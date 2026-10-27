// The WebGPU frame: the surface configuration, the depth attachment, and the
// one render pass of each frame. The swapchain rules are the same as in the
// Vulkan backend: r_swapInterval selects the present mode, an outdated or
// suboptimal surface is configured again before the next frame, and a frame
// is skipped when the window has a size of 0.

#include "wgpu_local.h"
#include "glConfig.h"

// r_swapInterval: 0 is no vertical sync, 1 is vertical sync, -1 is adaptive.
static int s_swapInterval = 0;

// r_apiCoverage heavy: the GPU time of the render pass of a frame, from two
// timestamp queries. The result comes back some frames later: the queries of
// each frame go into one slot of a ring, and a map without a wait reads them.
#define TIMING_SLOTS 3
#define TIMING_SLOT_STRIDE 256	// the offset of a query resolve must be a multiple of 256

typedef struct {
    WGPUQuerySet querySet;					// 2 timestamps for each slot
    WGPUBuffer resolveBuffer;				// TIMING_SLOT_STRIDE bytes for each slot
    WGPUBuffer readBuffers[TIMING_SLOTS];	// 2 timestamps that the CPU can read
    qboolean busy[TIMING_SLOTS];			// the map of the read buffer did not complete
    uint32_t next;							// the slot of the next frame
    int current;							// the slot of this frame, or -1
    qboolean valid;
    double gpuMs;							// the last result
    cvar_t* speeds;							// r_speeds
} wgpuTiming_t;

static wgpuTiming_t s_timing;


void wgpu_createTimingResources(void)
{
    memset(&s_timing, 0, sizeof(s_timing));
    s_timing.current = -1;
    if (!wg.timestampQuery)
        return;

    s_timing.speeds = ri.Cvar_Get("r_speeds", "0", CVAR_CHEAT);

    WGPUQuerySetDescriptor queryDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    queryDesc.label = wgpu_stringView("frame timestamps");
    queryDesc.type = WGPUQueryType_Timestamp;
    queryDesc.count = 2 * TIMING_SLOTS;
    s_timing.querySet = wgpuDeviceCreateQuerySet(wg.device, &queryDesc);

    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.label = wgpu_stringView("frame timestamps resolve");
    bufferDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
    bufferDesc.size = TIMING_SLOT_STRIDE * TIMING_SLOTS;
    s_timing.resolveBuffer = wgpuDeviceCreateBuffer(wg.device, &bufferDesc);

    uint32_t i;
    for (i = 0; i < TIMING_SLOTS; i++)
    {
        bufferDesc.label = wgpu_stringView("frame timestamps read");
        bufferDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bufferDesc.size = 2 * sizeof(uint64_t);
        s_timing.readBuffers[i] = wgpuDeviceCreateBuffer(wg.device, &bufferDesc);
    }
}


void wgpu_releaseTimingResources(void)
{
    uint32_t i;
    for (i = 0; i < TIMING_SLOTS; i++)
    {
        if (s_timing.readBuffers[i])
            wgpuBufferRelease(s_timing.readBuffers[i]);
    }
    if (s_timing.resolveBuffer)
        wgpuBufferRelease(s_timing.resolveBuffer);
    if (s_timing.querySet)
    {
        wgpuQuerySetDestroy(s_timing.querySet);
        wgpuQuerySetRelease(s_timing.querySet);
    }
    // A map that did not complete calls wgpu_onTimingMapped with a failure
    // status. That callback does not use the released objects.
    memset(&s_timing, 0, sizeof(s_timing));
    s_timing.current = -1;
}


// Select the slot of this frame. Return qfalse when no slot is free.
static qboolean wgpu_beginTiming(WGPUPassTimestampWrites* writes)
{
    s_timing.current = -1;
    if (s_timing.querySet == NULL || s_timing.busy[s_timing.next])
        return qfalse;

    s_timing.current = (int)s_timing.next;
    s_timing.next = (s_timing.next + 1) % TIMING_SLOTS;

    writes->querySet = s_timing.querySet;
    writes->beginningOfPassWriteIndex = 2 * (uint32_t)s_timing.current;
    writes->endOfPassWriteIndex = 2 * (uint32_t)s_timing.current + 1;
    return qtrue;
}


// After the render pass: copy the timestamps of this frame to the read buffer.
static void wgpu_resolveTiming(void)
{
    if (s_timing.current < 0)
        return;

    const uint32_t slot = (uint32_t)s_timing.current;
    wgpuCommandEncoderResolveQuerySet(wg.encoder, s_timing.querySet, 2 * slot, 2, s_timing.resolveBuffer,
            (uint64_t)TIMING_SLOT_STRIDE * slot);
    wgpuCommandEncoderCopyBufferToBuffer(wg.encoder, s_timing.resolveBuffer, (uint64_t)TIMING_SLOT_STRIDE * slot,
            s_timing.readBuffers[slot], 0, 2 * sizeof(uint64_t));
}


static void wgpu_onTimingMapped(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2)
{
    const uint32_t slot = (uint32_t)(uintptr_t)userdata1;
    s_timing.busy[slot] = qfalse;
    if (status != WGPUMapAsyncStatus_Success)
        return;

    const uint64_t* ticks = (const uint64_t*)wgpuBufferGetConstMappedRange(s_timing.readBuffers[slot], 0,
            2 * sizeof(uint64_t));
    // A timestamp is in nanoseconds. A browser can round the values, and an
    // end before the beginning is possible.
    if (ticks && ticks[1] >= ticks[0])
    {
        s_timing.gpuMs = (double)(ticks[1] - ticks[0]) / 1000000.0;
        s_timing.valid = qtrue;
    }
    wgpuBufferUnmap(s_timing.readBuffers[slot]);
}


// After the submit: start the map of the read buffer. The callback runs in the
// wgpuInstanceProcessEvents of a later gapi_beginFrame, so this does not wait.
static void wgpu_readTiming(void)
{
    if (s_timing.current < 0)
        return;

    const uint32_t slot = (uint32_t)s_timing.current;
    WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.callback = wgpu_onTimingMapped;
    callback.userdata1 = (void*)(uintptr_t)slot;
    s_timing.busy[slot] = qtrue;
    wgpuBufferMapAsync(s_timing.readBuffers[slot], WGPUMapMode_Read, 0, 2 * sizeof(uint64_t), callback);
    s_timing.current = -1;

    if (s_timing.valid && s_timing.speeds && s_timing.speeds->integer)
        ri.Printf(PRINT_ALL, "WebGPU GPU time of the render pass: %.3f ms\n", s_timing.gpuMs);
}


// r_apiCoverage heavy: the occlusion queries of gapi_beginOcclusionQuery. Each
// slot of a ring has GAPI_MAX_OCCLUSION_QUERIES queries in one query set, and
// the render pass of a frame uses the queries of one slot. As for the
// timestamps, a map without a wait reads the results some frames later.
#define OCCLUSION_SLOTS 3
#define OCCLUSION_SLOT_BYTES (GAPI_MAX_OCCLUSION_QUERIES * sizeof(uint64_t))	// a multiple of 256

typedef struct {
    WGPUQuerySet querySet;					// GAPI_MAX_OCCLUSION_QUERIES queries for each slot
    WGPUBuffer resolveBuffer;				// OCCLUSION_SLOT_BYTES for each slot
    WGPUBuffer readBuffers[OCCLUSION_SLOTS];
    qboolean busy[OCCLUSION_SLOTS];			// the map of the read buffer did not complete
    uint32_t count[OCCLUSION_SLOTS];		// the highest index that the frame used, plus 1
    uint32_t used[OCCLUSION_SLOTS][GAPI_MAX_OCCLUSION_QUERIES / 32];	// the indexes that the frame used
    uint32_t tags[OCCLUSION_SLOTS][GAPI_MAX_OCCLUSION_QUERIES];
    uint32_t next;							// the slot of the next frame
    int current;							// the slot of this frame, or -1
    qboolean open;							// a query started and did not end

    // The newest result of each index, until gapi_getOcclusionResult gives it.
    qboolean ready[GAPI_MAX_OCCLUSION_QUERIES];
    uint32_t resultTags[GAPI_MAX_OCCLUSION_QUERIES];
    uint64_t results[GAPI_MAX_OCCLUSION_QUERIES];
} wgpuOcclusion_t;

static wgpuOcclusion_t s_occlusion;


void wgpu_createOcclusionResources(void)
{
    memset(&s_occlusion, 0, sizeof(s_occlusion));
    s_occlusion.current = -1;
    if (!wg.heavyCoverage)
        return;

    WGPUQuerySetDescriptor queryDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    queryDesc.label = wgpu_stringView("occlusion queries");
    queryDesc.type = WGPUQueryType_Occlusion;
    queryDesc.count = GAPI_MAX_OCCLUSION_QUERIES * OCCLUSION_SLOTS;
    s_occlusion.querySet = wgpuDeviceCreateQuerySet(wg.device, &queryDesc);

    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.label = wgpu_stringView("occlusion queries resolve");
    bufferDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
    bufferDesc.size = OCCLUSION_SLOT_BYTES * OCCLUSION_SLOTS;
    s_occlusion.resolveBuffer = wgpuDeviceCreateBuffer(wg.device, &bufferDesc);

    uint32_t i;
    for (i = 0; i < OCCLUSION_SLOTS; i++)
    {
        bufferDesc.label = wgpu_stringView("occlusion queries read");
        bufferDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bufferDesc.size = OCCLUSION_SLOT_BYTES;
        s_occlusion.readBuffers[i] = wgpuDeviceCreateBuffer(wg.device, &bufferDesc);
    }
}


void wgpu_releaseOcclusionResources(void)
{
    uint32_t i;
    for (i = 0; i < OCCLUSION_SLOTS; i++)
    {
        if (s_occlusion.readBuffers[i])
            wgpuBufferRelease(s_occlusion.readBuffers[i]);
    }
    if (s_occlusion.resolveBuffer)
        wgpuBufferRelease(s_occlusion.resolveBuffer);
    if (s_occlusion.querySet)
    {
        wgpuQuerySetDestroy(s_occlusion.querySet);
        wgpuQuerySetRelease(s_occlusion.querySet);
    }
    // A map that did not complete calls wgpu_onOcclusionMapped with a failure
    // status. That callback does not use the released objects.
    memset(&s_occlusion, 0, sizeof(s_occlusion));
    s_occlusion.current = -1;
}


// Select the slot of this frame. Return qfalse when no slot is free: the
// queries of this frame then do nothing.
static qboolean wgpu_beginOcclusion(void)
{
    s_occlusion.current = -1;
    s_occlusion.open = qfalse;
    if (s_occlusion.querySet == NULL || s_occlusion.busy[s_occlusion.next])
        return qfalse;

    const uint32_t slot = s_occlusion.next;
    s_occlusion.current = (int)slot;
    s_occlusion.next = (slot + 1) % OCCLUSION_SLOTS;
    s_occlusion.count[slot] = 0;
    memset(s_occlusion.used[slot], 0, sizeof(s_occlusion.used[slot]));
    return qtrue;
}


void gapi_beginOcclusionQuery(uint32_t index, uint32_t tag)
{
    if (!wg.frameActive || s_occlusion.current < 0 || s_occlusion.open || index >= GAPI_MAX_OCCLUSION_QUERIES)
        return;

    const uint32_t slot = (uint32_t)s_occlusion.current;
    const uint32_t bit = 1u << (index % 32);
    if (s_occlusion.used[slot][index / 32] & bit)
        return;		// a render pass can use a query only one time

    s_occlusion.used[slot][index / 32] |= bit;
    s_occlusion.tags[slot][index] = tag;
    if (index + 1 > s_occlusion.count[slot])
        s_occlusion.count[slot] = index + 1;

    wgpuRenderPassEncoderBeginOcclusionQuery(wg.pass, slot * GAPI_MAX_OCCLUSION_QUERIES + index);
    s_occlusion.open = qtrue;
}


void gapi_endOcclusionQuery(void)
{
    if (!s_occlusion.open)
        return;
    wgpuRenderPassEncoderEndOcclusionQuery(wg.pass);
    s_occlusion.open = qfalse;
}


qboolean gapi_getOcclusionResult(uint32_t index, uint32_t* tag, uint64_t* samples)
{
    if (index >= GAPI_MAX_OCCLUSION_QUERIES || !s_occlusion.ready[index])
        return qfalse;

    s_occlusion.ready[index] = qfalse;
    *tag = s_occlusion.resultTags[index];
    *samples = s_occlusion.results[index];
    return qtrue;
}


// After the render pass: copy the results of this frame to the read buffer.
static void wgpu_resolveOcclusion(void)
{
    if (s_occlusion.current < 0)
        return;

    const uint32_t slot = (uint32_t)s_occlusion.current;
    const uint32_t count = s_occlusion.count[slot];
    if (count == 0)
    {
        s_occlusion.current = -1;	// no queries in this frame
        return;
    }

    wgpuCommandEncoderResolveQuerySet(wg.encoder, s_occlusion.querySet, slot * GAPI_MAX_OCCLUSION_QUERIES, count,
            s_occlusion.resolveBuffer, (uint64_t)OCCLUSION_SLOT_BYTES * slot);
    wgpuCommandEncoderCopyBufferToBuffer(wg.encoder, s_occlusion.resolveBuffer, (uint64_t)OCCLUSION_SLOT_BYTES * slot,
            s_occlusion.readBuffers[slot], 0, (uint64_t)count * sizeof(uint64_t));
}


static void wgpu_onOcclusionMapped(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2)
{
    const uint32_t slot = (uint32_t)(uintptr_t)userdata1;
    s_occlusion.busy[slot] = qfalse;
    if (status != WGPUMapAsyncStatus_Success)
        return;

    const uint32_t count = s_occlusion.count[slot];
    const uint64_t* samples = (const uint64_t*)wgpuBufferGetConstMappedRange(s_occlusion.readBuffers[slot], 0,
            (size_t)count * sizeof(uint64_t));
    if (samples)
    {
        uint32_t i;
        for (i = 0; i < count; i++)
        {
            if (!(s_occlusion.used[slot][i / 32] & (1u << (i % 32))))
                continue;
            s_occlusion.results[i] = samples[i];
            s_occlusion.resultTags[i] = s_occlusion.tags[slot][i];
            s_occlusion.ready[i] = qtrue;
        }
    }
    wgpuBufferUnmap(s_occlusion.readBuffers[slot]);
}


// After the submit: start the map of the read buffer, without a wait.
static void wgpu_readOcclusion(void)
{
    if (s_occlusion.current < 0)
        return;

    const uint32_t slot = (uint32_t)s_occlusion.current;
    WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.callback = wgpu_onOcclusionMapped;
    callback.userdata1 = (void*)(uintptr_t)slot;
    s_occlusion.busy[slot] = qtrue;
    wgpuBufferMapAsync(s_occlusion.readBuffers[slot], WGPUMapMode_Read, 0,
            (size_t)s_occlusion.count[slot] * sizeof(uint64_t), callback);
    s_occlusion.current = -1;
}


void wgpu_printTimingInfo(void)
{
    if (!wg.timestampQuery)
        ri.Printf(PRINT_ALL, "WebGPU timestamp queries: no\n");
    else if (!s_timing.valid)
        ri.Printf(PRINT_ALL, "WebGPU timestamp queries: yes, no result yet\n");
    else
        ri.Printf(PRINT_ALL, "WebGPU timestamp queries: yes, the last render pass took %.3f ms\n", s_timing.gpuMs);
}


static WGPUPresentMode wgpu_selectPresentMode(const WGPUSurfaceCapabilities* caps)
{
    // The present modes to try for each r_swapInterval, best first.
    // WGPUPresentMode_Fifo is always available.
    static const WGPUPresentMode noSync[] = { WGPUPresentMode_Mailbox, WGPUPresentMode_Immediate, WGPUPresentMode_Fifo };
    static const WGPUPresentMode sync[] = { WGPUPresentMode_Fifo };
    static const WGPUPresentMode adaptive[] = { WGPUPresentMode_FifoRelaxed, WGPUPresentMode_Fifo };

    const WGPUPresentMode* modes = noSync;
    size_t count = ARRAY_LEN(noSync);
    if (s_swapInterval > 0)
    {
        modes = sync;
        count = ARRAY_LEN(sync);
    }
    else if (s_swapInterval < 0)
    {
        modes = adaptive;
        count = ARRAY_LEN(adaptive);
    }

    size_t i, j;
    for (i = 0; i < count; i++)
        for (j = 0; j < caps->presentModeCount; j++)
            if (caps->presentModes[j] == modes[i])
                return modes[i];

    return WGPUPresentMode_Fifo;
}


static const char* wgpu_presentModeName(WGPUPresentMode mode)
{
    switch (mode)
    {
        case WGPUPresentMode_Fifo: return "Fifo";
        case WGPUPresentMode_FifoRelaxed: return "FifoRelaxed";
        case WGPUPresentMode_Immediate: return "Immediate";
        case WGPUPresentMode_Mailbox: return "Mailbox";
        default: return "unknown";
    }
}


static void wgpu_releaseDepth(void)
{
    if (wg.offscreenView)
        wgpuTextureViewRelease(wg.offscreenView);
    if (wg.offscreenTexture)
    {
        wgpuTextureDestroy(wg.offscreenTexture);
        wgpuTextureRelease(wg.offscreenTexture);
    }
    wg.offscreenView = NULL;
    wg.offscreenTexture = NULL;

    if (wg.depthView)
        wgpuTextureViewRelease(wg.depthView);
    if (wg.depthTexture)
    {
        wgpuTextureDestroy(wg.depthTexture);
        wgpuTextureRelease(wg.depthTexture);
    }
    wg.depthView = NULL;
    wg.depthTexture = NULL;
}


static void wgpu_releaseFrame(void)
{
    if (wg.pass)
        wgpuRenderPassEncoderRelease(wg.pass);
    if (wg.encoder)
        wgpuCommandEncoderRelease(wg.encoder);
    if (wg.frameView)
        wgpuTextureViewRelease(wg.frameView);
    if (wg.frameTexture)
        wgpuTextureRelease(wg.frameTexture);
    wg.pass = NULL;
    wg.encoder = NULL;
    wg.frameView = NULL;
    wg.frameTexture = NULL;
    wg.frameActive = qfalse;
}


void wgpu_releaseFrameResources(void)
{
    wgpu_releaseFrame();
    wgpu_releaseDepth();
}


// Configure the surface for the window size and r_swapInterval, and make the
// depth attachment. Return qfalse if the window has a size of 0.
qboolean wgpu_configureSurface(void)
{
    int width, height;
    R_GetWinResolution(&width, &height);
    if (width <= 0 || height <= 0)
    {
        wg.reconfigure = qtrue;
        return qfalse;
    }

    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuSurfaceGetCapabilities(wg.surface, wg.adapter, &caps) != WGPUStatus_Success || caps.formatCount == 0)
    {
        ri.Printf(PRINT_WARNING, "WebGPU: the surface has no capabilities\n");
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        return qfalse;
    }

    // Q3 does the gamma on the CPU, so the surface format must not be sRGB.
    // Dawn can report an sRGB format first.
    wg.surfaceFormat = WGPUTextureFormat_Undefined;
    size_t i;
#ifdef __EMSCRIPTEN__
    // The first format is the preferred canvas format of the browser. Another
    // format needs an extra copy for each frame.
    if (caps.formats[0] == WGPUTextureFormat_BGRA8Unorm || caps.formats[0] == WGPUTextureFormat_RGBA8Unorm)
        wg.surfaceFormat = caps.formats[0];
#endif
    for (i = 0; i < caps.formatCount && wg.surfaceFormat == WGPUTextureFormat_Undefined; i++)
        if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm)
            wg.surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    for (i = 0; i < caps.formatCount && wg.surfaceFormat == WGPUTextureFormat_Undefined; i++)
        if (caps.formats[i] == WGPUTextureFormat_RGBA8Unorm)
            wg.surfaceFormat = WGPUTextureFormat_RGBA8Unorm;
    if (wg.surfaceFormat == WGPUTextureFormat_Undefined)
    {
        ri.Printf(PRINT_WARNING, "WebGPU: the surface has no BGRA8Unorm or RGBA8Unorm format, use format %d\n",
                (int)caps.formats[0]);
        wg.surfaceFormat = caps.formats[0];
    }

    // Screenshots copy from the surface texture. If the surface cannot be
    // copied, the frame is drawn into an offscreen texture.
    wg.surfaceUsage = WGPUTextureUsage_RenderAttachment;
    if (caps.usages & WGPUTextureUsage_CopySrc)
        wg.surfaceUsage |= WGPUTextureUsage_CopySrc;
    wg.offscreen = !(wg.surfaceUsage & WGPUTextureUsage_CopySrc) ||
            (wg.forceOffscreen && wg.forceOffscreen->integer);

    wg.presentMode = wgpu_selectPresentMode(&caps);
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = wg.device;
    config.format = wg.surfaceFormat;
    config.usage = wg.surfaceUsage;
    config.width = width;
    config.height = height;
    config.alphaMode = WGPUCompositeAlphaMode_Opaque;
    config.presentMode = wg.presentMode;
    wgpuSurfaceConfigure(wg.surface, &config);
    wg.configured = qtrue;
    wg.width = width;
    wg.height = height;

    ri.Printf(PRINT_ALL, " WebGPU surface: %u x %u, format %d, present mode %s (r_swapInterval %d)\n",
            wg.width, wg.height, (int)wg.surfaceFormat, wgpu_presentModeName(wg.presentMode), s_swapInterval);
    if (wg.offscreen)
        ri.Printf(PRINT_ALL, " WebGPU: the frame is drawn into an offscreen texture%s\n",
                (wg.surfaceUsage & WGPUTextureUsage_CopySrc) ? " (r_webgpuOffscreen 1)" : ", because the surface cannot be copied");

    // The depth and stencil attachment. The stencil is for cg_shadows 2.
    wgpu_releaseDepth();
    WGPUTextureDescriptor depthDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    depthDesc.label = wgpu_stringView("depth");
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;
    depthDesc.dimension = WGPUTextureDimension_2D;
    depthDesc.size.width = wg.width;
    depthDesc.size.height = wg.height;
    depthDesc.size.depthOrArrayLayers = 1;
    depthDesc.format = WGPUTextureFormat_Depth24PlusStencil8;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    wg.depthTexture = wgpuDeviceCreateTexture(wg.device, &depthDesc);
    wg.depthView = wgpuTextureCreateView(wg.depthTexture, NULL);

    if (wg.offscreen)
    {
        WGPUTextureDescriptor colorDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        colorDesc.label = wgpu_stringView("offscreen color");
        colorDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
                WGPUTextureUsage_TextureBinding;
        colorDesc.dimension = WGPUTextureDimension_2D;
        colorDesc.size.width = wg.width;
        colorDesc.size.height = wg.height;
        colorDesc.size.depthOrArrayLayers = 1;
        colorDesc.format = wg.surfaceFormat;
        colorDesc.mipLevelCount = 1;
        colorDesc.sampleCount = 1;
        wg.offscreenTexture = wgpuDeviceCreateTexture(wg.device, &colorDesc);
        wg.offscreenView = wgpuTextureCreateView(wg.offscreenTexture, NULL);
    }

    wg.reconfigure = qfalse;
    return qtrue;
}


void gapi_setSwapInterval(int interval)
{
    if (interval == s_swapInterval)
        return;

    s_swapInterval = interval;
    if (wg.configured)
        wg.reconfigure = qtrue;
}


// Get the texture of the surface for this frame. Return qfalse if the frame
// must be skipped.
static qboolean wgpu_acquireSurfaceTexture(void)
{
    int attempt;
    for (attempt = 0; attempt < 2; attempt++)
    {
        WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(wg.surface, &surfaceTexture);

        switch (surfaceTexture.status)
        {
            case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
                wg.frameTexture = surfaceTexture.texture;
                return qtrue;

            case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
                // The texture is still usable. Configure the surface again
                // before the next frame.
                wg.frameTexture = surfaceTexture.texture;
                wg.reconfigure = qtrue;
                return qtrue;

            case WGPUSurfaceGetCurrentTextureStatus_Timeout:
            case WGPUSurfaceGetCurrentTextureStatus_Outdated:
            case WGPUSurfaceGetCurrentTextureStatus_Lost:
                if (surfaceTexture.texture)
                    wgpuTextureRelease(surfaceTexture.texture);
                if (!wgpu_configureSurface())
                    return qfalse;
                break;

            default:
                if (surfaceTexture.texture)
                    wgpuTextureRelease(surfaceTexture.texture);
                ri.Printf(PRINT_WARNING, "WebGPU: wgpuSurfaceGetCurrentTexture status %d\n",
                        (int)surfaceTexture.status);
                return qfalse;
        }
    }

    wg.reconfigure = qtrue;
    return qfalse;
}


void gapi_beginFrame(void)
{
    wg.frameActive = qfalse;
    if (!wg.initialized)
        return;

    // r_apiCoverage heavy: wait for the pipelines before the frame starts.
    wgpu_resolvePendingPipelines();

    // Run the callbacks: device loss, errors, and later, buffer maps.
    wgpuInstanceProcessEvents(wg.instance);

    if (wg.errorCount != 0 && wg.fatalErrors && wg.fatalErrors->integer)
        ri.Error(ERR_FATAL, "WebGPU: %u errors, and r_webgpuFatalErrors is 1\n", wg.errorCount);

    // r_apiCoverage heavy: the device-lost future is complete when the device
    // is lost. A timeout of 0 does not wait. Examine the future only one time.
    if (wg.heavyCoverage && wg.lostFuture.id != 0)
    {
        WGPUFutureWaitInfo wait = WGPU_FUTURE_WAIT_INFO_INIT;
        wait.future = wg.lostFuture;
        if (wgpuInstanceWaitAny(wg.instance, 1, &wait, 0) == WGPUWaitStatus_Success && wait.completed)
        {
            wg.lostFuture.id = 0;
            wg.deviceLost = qtrue;
        }
    }

    if (wg.deviceLost)
    {
        // Restart the renderer, as for other fatal graphics errors.
        wg.deviceLost = qfalse;
        ri.Cmd_ExecuteText(EXEC_APPEND, "vid_restart\n");
        return;
    }

    // The window size changes with vid_restart, and r_swapInterval at any time.
    int width, height;
    R_GetWinResolution(&width, &height);
    if ((uint32_t)width != wg.width || (uint32_t)height != wg.height)
        wg.reconfigure = qtrue;

    if (wg.reconfigure && !wgpu_configureSurface())
        return;

    if (!wgpu_acquireSurfaceTexture())
        return;

    wg.frameView = wgpuTextureCreateView(wg.frameTexture, NULL);
    wg.encoder = wgpuDeviceCreateCommandEncoder(wg.device, NULL);

    // Clear all attachments at the start of the frame, as the Vulkan render
    // pass does for the depth and the stencil.
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = wg.offscreen ? wg.offscreenView : wg.frameView;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue.r = 0.0;
    color.clearValue.g = 0.0;
    color.clearValue.b = 0.0;
    color.clearValue.a = 1.0;

    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = wg.depthView;
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Discard;
    depth.depthClearValue = 1.0f;
    depth.stencilLoadOp = WGPULoadOp_Clear;
    depth.stencilStoreOp = WGPUStoreOp_Discard;
    depth.stencilClearValue = 0;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;
    passDesc.depthStencilAttachment = &depth;

    // r_apiCoverage heavy: the timestamps at the start and the end of the pass.
    WGPUPassTimestampWrites timestamps = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    if (wgpu_beginTiming(&timestamps))
        passDesc.timestampWrites = &timestamps;
    // r_apiCoverage heavy: the occlusion queries of the flares.
    if (wgpu_beginOcclusion())
        passDesc.occlusionQuerySet = s_occlusion.querySet;

    wg.pass = wgpuCommandEncoderBeginRenderPass(wg.encoder, &passDesc);

    wgpu_beginGeometryFrame();
    wg.frameActive = qtrue;
}


void gapi_endFrame(void)
{
    // gapi_beginFrame skipped the frame: there is nothing to submit.
    if (!wg.frameActive)
        return;

    gapi_endOcclusionQuery();
    wgpuRenderPassEncoderEnd(wg.pass);
    wgpu_resolveTiming();
    wgpu_resolveOcclusion();

    // Write the vertex data and the vertex shader constants of the frame.
    // The queue does the writes before the commands of the frame.
    wgpu_writeGeometryFrame();

    // r_apiCoverage heavy: a debug group for the copies after the render pass.
    if (wg.heavyCoverage)
        wgpuCommandEncoderPushDebugGroup(wg.encoder, wgpu_stringView("end of frame copies"));

    // Keep a copy for screenshots and video capture.
    if (wg.heavyCoverage)
        wgpuCommandEncoderInsertDebugMarker(wg.encoder, wgpu_stringView("keep the last frame"));
    wgpu_keepLastFrame();

    if (wg.offscreen)
    {
        if (wg.heavyCoverage)
            wgpuCommandEncoderInsertDebugMarker(wg.encoder, wgpu_stringView("copy to the surface"));
        wgpu_blitToSurface();
    }

    if (wg.heavyCoverage)
        wgpuCommandEncoderPopDebugGroup(wg.encoder);

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(wg.encoder, NULL);
    wgpuQueueSubmit(wg.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpu_readTiming();
    wgpu_readOcclusion();

    // The only call to wgpuSurfacePresent. A browser presents by itself.
#ifndef __EMSCRIPTEN__
    WGPUStatus status = wgpuSurfacePresent(wg.surface);
    if (status != WGPUStatus_Success)
        wg.reconfigure = qtrue;
#endif

    wgpu_releaseFrame();
}

// The WebGPU frame: the surface configuration, the depth attachment, and the
// one render pass of each frame. The swapchain rules are the same as in the
// Vulkan backend: r_swapInterval selects the present mode, an outdated or
// suboptimal surface is configured again before the next frame, and a frame
// is skipped when the window has a size of 0.

#include "wgpu_local.h"
#include "glConfig.h"

// r_swapInterval: 0 is no vertical sync, 1 is vertical sync, -1 is adaptive.
static int s_swapInterval = 0;


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
    wg.pass = wgpuCommandEncoderBeginRenderPass(wg.encoder, &passDesc);

    wgpu_beginGeometryFrame();
    wg.frameActive = qtrue;
}


void gapi_endFrame(void)
{
    // gapi_beginFrame skipped the frame: there is nothing to submit.
    if (!wg.frameActive)
        return;

    wgpuRenderPassEncoderEnd(wg.pass);

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

    // The only call to wgpuSurfacePresent. A browser presents by itself.
#ifndef __EMSCRIPTEN__
    WGPUStatus status = wgpuSurfacePresent(wg.surface);
    if (status != WGPUStatus_Success)
        wg.reconfigure = qtrue;
#endif

    wgpu_releaseFrame();
}

// The WebGPU clears and the pixel readback.
//
// WebGPU has no clear command inside a render pass. A clear draws a triangle
// that covers the viewport, with the scissor rectangle of the clear, as
// vkCmdClearAttachments clears only its rectangle.
//
// Q3 reads the pixels of a frame after the frame is presented (RC_SCREENSHOT
// and RC_VIDEOFRAME come before the next frame). The surface texture is not
// valid after wgpuSurfacePresent, so gapi_endFrame copies it to a texture
// first, and gapi_readPixels reads that texture.
//
// If the surface cannot be copied (no CopySrc usage), the frame is drawn into
// an offscreen texture (wgpu_frame.c). gapi_endFrame copies that texture for
// gapi_readPixels, and then draws it into the surface texture.

#include "wgpu_local.h"

// The clear shaders. They are not Q3 shaders, so they are WGSL here, not
// GLSL in graphics_api/shaders/glsl.
static const char s_clearShader[] =
    "@vertex fn vs(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {\n"
    "    // One triangle that covers the viewport.\n"
    "    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return vec4f(p * 2.0 - 1.0, 0.5, 1.0);\n"
    "}\n"
    "@fragment fn fs() -> @location(0) vec4f {\n"
    "    // The blend state gives the color: the blend constant.\n"
    "    return vec4f(0.0, 0.0, 0.0, 0.0);\n"
    "}\n";

// The copy of the offscreen texture to the surface: one triangle that covers
// the surface, and a fragment shader that loads the pixel at its position.
static const char s_blitShader[] =
    "@group(0) @binding(0) var source : texture_2d<f32>;\n"
    "@vertex fn vs(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {\n"
    "    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return vec4f(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n"
    "@fragment fn fs(@builtin(position) position : vec4f) -> @location(0) vec4f {\n"
    "    return textureLoad(source, vec2i(position.xy), 0);\n"
    "}\n";

static WGPURenderPipeline s_blitPipeline;
static WGPUBindGroup s_blitBindGroup;
static WGPUTextureView s_blitSource;      // the view in s_blitBindGroup

typedef enum {
    CLEAR_COLOR,
    CLEAR_DEPTH,
    CLEAR_DEPTH_STENCIL,
    CLEAR_COUNT
} clearType_t;

static WGPURenderPipeline s_clearPipelines[CLEAR_COUNT];

// The last frame, for gapi_readPixels.
static WGPUTexture s_lastFrame;
static uint32_t s_lastFrameWidth, s_lastFrameHeight;
static WGPUTextureFormat s_lastFrameFormat;
static qboolean s_lastFrameValid;


static WGPURenderPipeline wgpu_createClearPipeline(WGPUShaderModule module, WGPUPipelineLayout layout, clearType_t type)
{
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = wgpu_stringView("clear");
    desc.layout = layout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = wgpu_stringView("vs");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.primitive.cullMode = WGPUCullMode_None;

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
    depthStencil.depthCompare = WGPUCompareFunction_Always;
    depthStencil.depthWriteEnabled = (type == CLEAR_COLOR) ? WGPUOptionalBool_False : WGPUOptionalBool_True;
    depthStencil.stencilFront.compare = WGPUCompareFunction_Always;
    depthStencil.stencilFront.failOp = WGPUStencilOperation_Keep;
    depthStencil.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    depthStencil.stencilFront.passOp = WGPUStencilOperation_Replace;
    depthStencil.stencilBack = depthStencil.stencilFront;
    depthStencil.stencilReadMask = 0xFF;
    // The stencil reference is 0, so the clear writes 0.
    depthStencil.stencilWriteMask = (type == CLEAR_DEPTH_STENCIL) ? 0xFF : 0;
    desc.depthStencil = &depthStencil;

    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFF;

    // The color clear writes the blend constant: src * constant + dst * 0.
    WGPUBlendState blend;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_Constant;
    blend.color.dstFactor = WGPUBlendFactor_Zero;
    blend.alpha = blend.color;

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = wg.surfaceFormat;
    target.writeMask = (type == CLEAR_COLOR) ? WGPUColorWriteMask_All : WGPUColorWriteMask_None;
    target.blend = (type == CLEAR_COLOR) ? &blend : NULL;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = wgpu_stringView("fs");
    fragment.targetCount = 1;
    fragment.targets = &target;
    desc.fragment = &fragment;

    return wgpuDeviceCreateRenderPipeline(wg.device, &desc);
}


void wgpu_createDrawResources(void)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = wgpu_stringView(s_clearShader);
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &source.chain;
    moduleDesc.label = wgpu_stringView("clear");
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(wg.device, &moduleDesc);

    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(wg.device, &layoutDesc);

    int i;
    for (i = 0; i < CLEAR_COUNT; i++)
        s_clearPipelines[i] = wgpu_createClearPipeline(module, layout, (clearType_t)i);

    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(module);

    // The blit pipeline. Its bind group layout comes from the shader.
    source.code = wgpu_stringView(s_blitShader);
    moduleDesc.label = wgpu_stringView("blit");
    module = wgpuDeviceCreateShaderModule(wg.device, &moduleDesc);

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = wg.surfaceFormat;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = wgpu_stringView("fs");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = wgpu_stringView("blit");
    desc.vertex.module = module;
    desc.vertex.entryPoint = wgpu_stringView("vs");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.primitive.cullMode = WGPUCullMode_None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFF;
    desc.fragment = &fragment;
    s_blitPipeline = wgpuDeviceCreateRenderPipeline(wg.device, &desc);

    wgpuShaderModuleRelease(module);
}


void wgpu_releaseDrawResources(void)
{
    int i;
    for (i = 0; i < CLEAR_COUNT; i++)
    {
        if (s_clearPipelines[i])
            wgpuRenderPipelineRelease(s_clearPipelines[i]);
        s_clearPipelines[i] = NULL;
    }
    if (s_lastFrame)
    {
        wgpuTextureDestroy(s_lastFrame);
        wgpuTextureRelease(s_lastFrame);
    }
    s_lastFrame = NULL;
    s_lastFrameValid = qfalse;

    if (s_blitBindGroup)
        wgpuBindGroupRelease(s_blitBindGroup);
    if (s_blitPipeline)
        wgpuRenderPipelineRelease(s_blitPipeline);
    s_blitBindGroup = NULL;
    s_blitPipeline = NULL;
    s_blitSource = NULL;
}


// Draw a clear triangle in the rectangle. Return qfalse if the rectangle is
// empty after the clamp to the surface.
static qboolean wgpu_clearRect(const gapi_rect_t* rect, float depth)
{
    const int x0 = MAX(rect->x, 0);
    const int y0 = MAX(rect->y, 0);
    const int x1 = MIN(rect->x + rect->width, (int)wg.width);
    const int y1 = MIN(rect->y + rect->height, (int)wg.height);
    if (x1 <= x0 || y1 <= y0)
        return qfalse;

    // The viewport depth range gives the depth that the clear writes.
    wgpuRenderPassEncoderSetViewport(wg.pass, (float)x0, (float)y0, (float)(x1 - x0), (float)(y1 - y0), depth, depth);
    wgpuRenderPassEncoderSetScissorRect(wg.pass, x0, y0, x1 - x0, y1 - y0);
    wgpuRenderPassEncoderDraw(wg.pass, 3, 1, 0, 0);
    return qtrue;
}


void gapi_clearColor(const gapi_rect_t* rect, const float color[4])
{
    if (!wg.frameActive)
        return;

    WGPUColor constant = { color[0], color[1], color[2], color[3] };
    wgpuRenderPassEncoderSetPipeline(wg.pass, s_clearPipelines[CLEAR_COLOR]);
    wgpuRenderPassEncoderSetBlendConstant(wg.pass, &constant);
    wgpu_clearRect(rect, 0.0f);
}


void gapi_clearDepthStencil(const gapi_rect_t* rect, qboolean stencil)
{
    if (!wg.frameActive)
        return;

    wgpuRenderPassEncoderSetPipeline(wg.pass, s_clearPipelines[stencil ? CLEAR_DEPTH_STENCIL : CLEAR_DEPTH]);
    wgpuRenderPassEncoderSetStencilReference(wg.pass, 0);
    wgpu_clearRect(rect, 1.0f);
}


// Copy the surface texture of the frame before the present. The render pass
// of the frame has ended.
void wgpu_keepLastFrame(void)
{
    // The texture that the render pass drew.
    WGPUTexture drawn = wg.offscreen ? wg.offscreenTexture : wg.frameTexture;
    if (!wg.offscreen && !(wg.surfaceUsage & WGPUTextureUsage_CopySrc))
        return;

    if (s_lastFrame == NULL || s_lastFrameWidth != wg.width || s_lastFrameHeight != wg.height ||
        s_lastFrameFormat != wg.surfaceFormat)
    {
        if (s_lastFrame)
        {
            wgpuTextureDestroy(s_lastFrame);
            wgpuTextureRelease(s_lastFrame);
        }

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label = wgpu_stringView("last frame");
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        desc.dimension = WGPUTextureDimension_2D;
        desc.size.width = wg.width;
        desc.size.height = wg.height;
        desc.size.depthOrArrayLayers = 1;
        desc.format = wg.surfaceFormat;
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;
        s_lastFrame = wgpuDeviceCreateTexture(wg.device, &desc);
        s_lastFrameWidth = wg.width;
        s_lastFrameHeight = wg.height;
        s_lastFrameFormat = wg.surfaceFormat;
    }

    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    src.texture = drawn;
    WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    dst.texture = s_lastFrame;
    WGPUExtent3D extent = { wg.width, wg.height, 1 };
    wgpuCommandEncoderCopyTextureToTexture(wg.encoder, &src, &dst, &extent);
    s_lastFrameValid = qtrue;
}


void wgpu_blitToSurface(void)
{
    if (s_blitSource != wg.offscreenView)
    {
        // The offscreen texture is new (surface configuration).
        if (s_blitBindGroup)
            wgpuBindGroupRelease(s_blitBindGroup);

        WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(s_blitPipeline, 0);
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.textureView = wg.offscreenView;
        WGPUBindGroupDescriptor groupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        groupDesc.label = wgpu_stringView("blit");
        groupDesc.layout = layout;
        groupDesc.entryCount = 1;
        groupDesc.entries = &entry;
        s_blitBindGroup = wgpuDeviceCreateBindGroup(wg.device, &groupDesc);
        wgpuBindGroupLayoutRelease(layout);
        s_blitSource = wg.offscreenView;
    }

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = wg.frameView;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    // The triangle covers the surface, so the clear only gives a defined start.
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.label = wgpu_stringView("blit");
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(wg.encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, s_blitPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, s_blitBindGroup, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}


static void wgpu_onMapped(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2)
{
    *(WGPUMapAsyncStatus*)userdata1 = status;
}


void gapi_readPixels(unsigned char* rgba, uint32_t width, uint32_t height)
{
    memset(rgba, 0, (size_t)width * height * 4);

    if (!wg.initialized || !s_lastFrameValid)
        return;

    width = MIN(width, s_lastFrameWidth);
    height = MIN(height, s_lastFrameHeight);

    // A buffer row must be a multiple of 256 bytes.
    const uint32_t rowSize = width * 4;
    const uint32_t bytesPerRow = (rowSize + 255) & ~255u;
    const uint64_t size = (uint64_t)bytesPerRow * height;

    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.label = wgpu_stringView("read pixels");
    bufferDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    bufferDesc.size = size;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(wg.device, &bufferDesc);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(wg.device, NULL);
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    src.texture = s_lastFrame;
    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dst.buffer = buffer;
    dst.layout.bytesPerRow = bytesPerRow;
    dst.layout.rowsPerImage = height;
    WGPUExtent3D extent = { width, height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(wg.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    // The only wait after startup. Screenshots and video capture need the
    // pixels now. A browser build cannot wait here.
    WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
    WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_WaitAnyOnly;
    callback.callback = wgpu_onMapped;
    callback.userdata1 = &status;
    WGPUFutureWaitInfo wait = WGPU_FUTURE_WAIT_INFO_INIT;
    wait.future = wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, size, callback);
    wgpuInstanceWaitAny(wg.instance, 1, &wait, UINT64_MAX);

    if (status == WGPUMapAsyncStatus_Success)
    {
        const unsigned char* data = (const unsigned char*)wgpuBufferGetConstMappedRange(buffer, 0, size);
        const qboolean bgra = (s_lastFrameFormat == WGPUTextureFormat_BGRA8Unorm);
        uint32_t y, x;

        // Give RGBA, the top row first.
        for (y = 0; y < height; y++)
        {
            const unsigned char* srcRow = data + (size_t)y * bytesPerRow;
            unsigned char* dstRow = rgba + (size_t)y * rowSize;
            if (!bgra)
            {
                memcpy(dstRow, srcRow, rowSize);
                continue;
            }
            for (x = 0; x < width; x++)
            {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
                dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
                dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
            }
        }
        wgpuBufferUnmap(buffer);
    }
    else
    {
        ri.Printf(PRINT_WARNING, "WebGPU: cannot map the pixels (%d)\n", (int)status);
    }

    wgpuBufferRelease(buffer);
}

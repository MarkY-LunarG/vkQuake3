// The WebGPU textures. Each texture has its own bind group with its view and
// its sampler, as each Vulkan image has its own descriptor set.

#include "wgpu_local.h"

static uint32_t s_numTextures = 0;
static gapi_texture_t* s_textures = NULL;		// the list of live textures


// r_apiCoverage heavy: the compute pass that makes the mipmaps on the GPU
// (gapi_generateMipmaps). It does the integer arithmetic of R_MipMap: the sum
// of 2x2 texels (2 texels for a level with a size of 1), shifted right. An
// rgba8unorm texel is v/255, and the store of v/255 gives v again, so the
// levels are the same as the levels of R_MipMap.
static const char s_mipmapShader[] =
    "@group(0) @binding(0) var src : texture_2d<f32>;\n"
    "@group(0) @binding(1) var dst : texture_storage_2d<rgba8unorm, write>;\n"
    "\n"
    "fn texel(p : vec2<i32>) -> vec4<u32> {\n"
    "    return vec4<u32>(round(textureLoad(src, p, 0) * 255.0));\n"
    "}\n"
    "\n"
    "@compute @workgroup_size(8, 8)\n"
    "fn main(@builtin(global_invocation_id) id : vec3<u32>) {\n"
    "    let outSize = textureDimensions(dst);\n"
    "    if (id.x >= outSize.x || id.y >= outSize.y) {\n"
    "        return;\n"
    "    }\n"
    "    let inSize = textureDimensions(src);\n"
    "    let p = vec2<i32>(id.xy) * 2;\n"
    "    var sum : vec4<u32>;\n"
    "    if (inSize.x > 1u && inSize.y > 1u) {\n"
    "        sum = (texel(p) + texel(p + vec2<i32>(1, 0)) + texel(p + vec2<i32>(0, 1)) + texel(p + vec2<i32>(1, 1))) >> vec4<u32>(2u);\n"
    "    } else if (inSize.x > 1u) {\n"
    "        sum = (texel(p) + texel(p + vec2<i32>(1, 0))) >> vec4<u32>(1u);\n"
    "    } else {\n"
    "        sum = (texel(p) + texel(p + vec2<i32>(0, 1))) >> vec4<u32>(1u);\n"
    "    }\n"
    "    textureStore(dst, vec2<i32>(id.xy), vec4<f32>(sum) / 255.0);\n"
    "}\n";

static WGPUBindGroupLayout s_mipmapLayout = NULL;
static WGPUComputePipeline s_mipmapPipeline = NULL;


static void wgpu_onComputePipelineCreated(WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline,
        WGPUStringView message, void* userdata1, void* userdata2)
{
    if (status == WGPUCreatePipelineAsyncStatus_Success)
    {
        s_mipmapPipeline = pipeline;
        return;
    }

    char text[1024];
    ri.Printf(PRINT_WARNING, "WebGPU: the mipmap pipeline failed: %s\n", wgpu_cString(message, text, sizeof(text)));
    if (pipeline)
        wgpuComputePipelineRelease(pipeline);
}


// Make the mipmap pipeline. This is at startup, not in a frame, so the wait
// for the asynchronous build is permitted in a browser.
static void wgpu_createMipmapResources(void)
{
    WGPUBindGroupLayoutEntry entries[2] = { WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT };
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[1].storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
    entries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = wgpu_stringView("mipmap");
    layoutDesc.entryCount = 2;
    layoutDesc.entries = entries;
    s_mipmapLayout = wgpuDeviceCreateBindGroupLayout(wg.device, &layoutDesc);

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.label = wgpu_stringView("mipmap");
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &s_mipmapLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(wg.device, &pipelineLayoutDesc);

    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = wgpu_stringView(s_mipmapShader);
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &source.chain;
    moduleDesc.label = wgpu_stringView("mipmap.comp");
    wgpu_pushErrorScope();
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(wg.device, &moduleDesc);
    wgpu_popErrorScope("mipmap.comp");

    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.label = wgpu_stringView("mipmap");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = wgpu_stringView("main");

    WGPUCreateComputePipelineAsyncCallbackInfo callback = WGPU_CREATE_COMPUTE_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_WaitAnyOnly;
    callback.callback = wgpu_onComputePipelineCreated;
    WGPUFutureWaitInfo wait = WGPU_FUTURE_WAIT_INFO_INIT;
    wait.future = wgpuDeviceCreateComputePipelineAsync(wg.device, &pipelineDesc, callback);
    wgpuInstanceWaitAny(wg.instance, 1, &wait, UINT64_MAX);

    wgpuShaderModuleRelease(module);
    wgpuPipelineLayoutRelease(pipelineLayout);
}


static void wgpu_releaseMipmapResources(void)
{
    if (s_mipmapPipeline)
        wgpuComputePipelineRelease(s_mipmapPipeline);
    if (s_mipmapLayout)
        wgpuBindGroupLayoutRelease(s_mipmapLayout);
    s_mipmapPipeline = NULL;
    s_mipmapLayout = NULL;
}


qboolean wgpu_mipmapGeneration(void)
{
    return (s_mipmapPipeline != NULL) ? qtrue : qfalse;
}


void gapi_generateMipmaps(gapi_texture_t* tex)
{
    if (s_mipmapPipeline == NULL || tex->mipLevels < 2)
        return;

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(wg.device, NULL);
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    passDesc.label = wgpu_stringView("mipmaps");
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderPushDebugGroup(pass, wgpu_stringView("mipmaps"));
    wgpuComputePassEncoderSetPipeline(pass, s_mipmapPipeline);

    uint32_t width = tex->width;
    uint32_t height = tex->height;
    uint32_t level;
    for (level = 1; level < tex->mipLevels; level++)
    {
        width = (width > 1) ? width >> 1 : 1;
        height = (height > 1) ? height >> 1 : 1;

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel = level - 1;
        viewDesc.mipLevelCount = 1;
        viewDesc.arrayLayerCount = 1;
        WGPUTextureView srcView = wgpuTextureCreateView(tex->texture, &viewDesc);
        viewDesc.baseMipLevel = level;
        WGPUTextureView dstView = wgpuTextureCreateView(tex->texture, &viewDesc);

        WGPUBindGroupEntry entries[2] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
        entries[0].binding = 0;
        entries[0].textureView = srcView;
        entries[1].binding = 1;
        entries[1].textureView = dstView;
        WGPUBindGroupDescriptor groupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        groupDesc.layout = s_mipmapLayout;
        groupDesc.entryCount = 2;
        groupDesc.entries = entries;
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(wg.device, &groupDesc);

        wgpuComputePassEncoderInsertDebugMarker(pass, wgpu_stringView("level"));
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, (width + 7) / 8, (height + 7) / 8, 1);

        wgpuBindGroupRelease(group);
        wgpuTextureViewRelease(dstView);
        wgpuTextureViewRelease(srcView);
    }

    wgpuComputePassEncoderPopDebugGroup(pass);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    // The queue does this work before the commands of the next frame.
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(wg.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}


void wgpu_createTextureResources(void)
{
    // Group 1 and group 2: the texture at binding 0, the sampler at binding 1.
    WGPUBindGroupLayoutEntry entries[2] = { WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT };
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = wgpu_stringView("texture");
    layoutDesc.entryCount = 2;
    layoutDesc.entries = entries;
    wg.textureLayout = wgpuDeviceCreateBindGroupLayout(wg.device, &layoutDesc);

    // The same samplers as the Vulkan backend (vk_find_sampler): linear
    // magnification and minification. Mipmapped textures use the nearest
    // mipmap. Other textures keep to level 0 with a maximum LOD of 0.25, as
    // OpenGL does for GL_LINEAR minification.
    int mipmap, repeat;
    for (mipmap = 0; mipmap < 2; mipmap++)
    {
        for (repeat = 0; repeat < 2; repeat++)
        {
            WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
            desc.addressModeU = repeat ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
            desc.addressModeV = desc.addressModeU;
            desc.addressModeW = desc.addressModeU;
            desc.magFilter = WGPUFilterMode_Linear;
            desc.minFilter = WGPUFilterMode_Linear;
            desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
            desc.lodMinClamp = 0.0f;
            desc.lodMaxClamp = mipmap ? 12.0f : 0.25f;
            desc.maxAnisotropy = 1;
            wg.samplers[mipmap][repeat] = wgpuDeviceCreateSampler(wg.device, &desc);
        }
    }

    if (wg.heavyCoverage)
        wgpu_createMipmapResources();
}


void wgpu_releaseTextureResources(void)
{
    wgpu_releaseMipmapResources();

    int mipmap, repeat;
    for (mipmap = 0; mipmap < 2; mipmap++)
    {
        for (repeat = 0; repeat < 2; repeat++)
        {
            if (wg.samplers[mipmap][repeat])
                wgpuSamplerRelease(wg.samplers[mipmap][repeat]);
            wg.samplers[mipmap][repeat] = NULL;
        }
    }
    if (wg.textureLayout)
        wgpuBindGroupLayoutRelease(wg.textureLayout);
    wg.textureLayout = NULL;
}


gapi_texture_t* gapi_createTexture(uint32_t width, uint32_t height, uint32_t mipLevels,
        qboolean mipmap, qboolean repeat)
{
    gapi_texture_t* tex = (gapi_texture_t*) calloc(1, sizeof(gapi_texture_t));
    if (tex == NULL)
    {
        ri.Error(ERR_FATAL, "gapi_createTexture: out of memory\n");
    }

    tex->width = width;
    tex->height = height;
    tex->mipLevels = mipLevels;
    tex->mipmap = mipmap;
    tex->repeat = repeat;

    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    // gapi_generateMipmaps writes levels 1 and higher as storage textures.
    if (s_mipmapPipeline != NULL && mipLevels > 1)
        desc.usage |= WGPUTextureUsage_StorageBinding;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size.width = width;
    desc.size.height = height;
    desc.size.depthOrArrayLayers = 1;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.mipLevelCount = mipLevels;
    desc.sampleCount = 1;
    tex->texture = wgpuDeviceCreateTexture(wg.device, &desc);
    tex->view = wgpuTextureCreateView(tex->texture, NULL);

    WGPUBindGroupEntry entries[2] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
    entries[0].binding = 0;
    entries[0].textureView = tex->view;
    entries[1].binding = 1;
    entries[1].sampler = wg.samplers[mipmap ? 1 : 0][repeat ? 1 : 0];

    WGPUBindGroupDescriptor groupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    groupDesc.layout = wg.textureLayout;
    groupDesc.entryCount = 2;
    groupDesc.entries = entries;
    tex->bindGroup = wgpuDeviceCreateBindGroup(wg.device, &groupDesc);

    tex->next = s_textures;
    if (s_textures)
        s_textures->prev = tex;
    s_textures = tex;

    ++s_numTextures;
    return tex;
}


void gapi_uploadTexture(gapi_texture_t* tex, const unsigned char* rgba, uint32_t numLevels)
{
    // The levels are packed in rgba, from level 0. Each level is half the
    // size of the level before it, and at least 1 pixel. wgpuQueueWriteTexture
    // needs no row alignment.
    uint32_t width = tex->width;
    uint32_t height = tex->height;
    uint32_t level;

    for (level = 0; level < numLevels; level++)
    {
        const size_t size = (size_t)width * height * 4;

        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = tex->texture;
        dst.mipLevel = level;
        dst.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = 0;
        layout.bytesPerRow = width * 4;
        layout.rowsPerImage = height;

        WGPUExtent3D extent = { width, height, 1 };
        wgpuQueueWriteTexture(wg.queue, &dst, rgba, size, &layout, &extent);

        rgba += size;
        width = (width > 1) ? width >> 1 : 1;
        height = (height > 1) ? height >> 1 : 1;
    }
}


void gapi_setTextureName(gapi_texture_t* tex, const char* name)
{
    // The default path sets no label. gapi_createTexture has no name for the
    // descriptor.
    if (wg.heavyCoverage)
        wgpuTextureSetLabel(tex->texture, wgpu_stringView(name));
}


void gapi_destroyTexture(gapi_texture_t* tex)
{
    // WebGPU keeps the texture until the submitted work that uses it is done.
    if (tex->bindGroup)
        wgpuBindGroupRelease(tex->bindGroup);
    if (tex->view)
        wgpuTextureViewRelease(tex->view);
    if (tex->texture)
        wgpuTextureRelease(tex->texture);

    if (tex->prev)
        tex->prev->next = tex->next;
    else
        s_textures = tex->next;
    if (tex->next)
        tex->next->prev = tex->prev;

    free(tex);
    --s_numTextures;
}


// r_apiCoverage heavy: the bytes of a texture, from the properties that WebGPU
// gives. The size of a depth texel is not known; use 4 bytes.
static uint64_t wgpu_textureBytes(WGPUTexture texture)
{
    if (texture == NULL)
        return 0;
    if (wgpuTextureGetDimension(texture) != WGPUTextureDimension_2D)
        return 0;

    const WGPUTextureFormat format = wgpuTextureGetFormat(texture);
    const uint64_t texelBytes = (format == WGPUTextureFormat_R8Unorm) ? 1 : 4;
    // The usage is not necessary for the sum. It is here for the API coverage.
    const uint64_t copies = (wgpuTextureGetUsage(texture) == WGPUTextureUsage_None) ? 0 : 1;

    uint64_t width = wgpuTextureGetWidth(texture);
    uint64_t height = wgpuTextureGetHeight(texture);
    const uint32_t levels = wgpuTextureGetMipLevelCount(texture);
    uint64_t bytes = 0;
    uint32_t level;

    for (level = 0; level < levels; level++)
    {
        bytes += width * height * texelBytes;
        width = (width > 1) ? width >> 1 : 1;
        height = (height > 1) ? height >> 1 : 1;
    }

    return bytes * wgpuTextureGetDepthOrArrayLayers(texture) * wgpuTextureGetSampleCount(texture) * copies;
}


void gapi_printMemoryInfo(void)
{
    ri.Printf(PRINT_ALL, "Number of textures: %u\n", s_numTextures);

    if (!wg.heavyCoverage)
        return;

    uint64_t textureBytes = 0;
    const gapi_texture_t* tex;
    for (tex = s_textures; tex; tex = tex->next)
        textureBytes += wgpu_textureBytes(tex->texture);

    const uint64_t attachmentBytes = wgpu_textureBytes(wg.depthTexture) + wgpu_textureBytes(wg.offscreenTexture);
    uint32_t bufferCount = 0;
    const uint64_t bufferBytes = wgpu_geometryBufferBytes(&bufferCount);

    ri.Printf(PRINT_ALL, "Texture memory: %.1f MB\n", textureBytes / (1024.0 * 1024.0));
    ri.Printf(PRINT_ALL, "Attachment memory: %.1f MB\n", attachmentBytes / (1024.0 * 1024.0));
    ri.Printf(PRINT_ALL, "Geometry buffer memory: %.1f MB in %u buffers\n",
            bufferBytes / (1024.0 * 1024.0), bufferCount);
}

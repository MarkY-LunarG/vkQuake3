// The WebGPU textures. Each texture has its own bind group with its view and
// its sampler, as each Vulkan image has its own descriptor set.

#include "wgpu_local.h"

static uint32_t s_numTextures = 0;
static gapi_texture_t* s_textures = NULL;		// the list of live textures


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
}


void wgpu_releaseTextureResources(void)
{
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

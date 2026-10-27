// The WebGPU draws: the vertex shader constants, the geometry buffers, the
// texture binds, and the draw commands.
//
// WebGPU has no push constants and no buffers that stay mapped while the GPU
// reads them. Thus the backend collects the data of a frame in CPU arrays,
// records the draws with offsets into the buffers, and writes the arrays to
// the buffers with wgpuQueueWriteBuffer before the submit. The queue does the
// writes before the command buffer runs.

#include "wgpu_local.h"

// The same sizes as the Vulkan backend (vk_geometry.c).
#define VERTEX_CHUNK_SIZE   (768 * 1024)
#define INDEX_BUFFER_SIZE   (2 * 1024 * 1024)

#define XYZ_SIZE            (4 * VERTEX_CHUNK_SIZE)
#define COLOR_SIZE          (1 * VERTEX_CHUNK_SIZE)
#define ST0_SIZE            (2 * VERTEX_CHUNK_SIZE)
#define ST1_SIZE            (2 * VERTEX_CHUNK_SIZE)

#define XYZ_OFFSET          0
#define COLOR_OFFSET        (XYZ_OFFSET + XYZ_SIZE)
#define ST0_OFFSET          (COLOR_OFFSET + COLOR_SIZE)
#define ST1_OFFSET          (ST0_OFFSET + ST0_SIZE)
#define VERTEX_BUFFER_SIZE  (ST1_OFFSET + ST1_SIZE)

// Each draw has its vertex shader constants in a slot of the uniform buffer.
// The dynamic offsets must be a multiple of 256.
#define CONSTANTS_SLOT_SIZE     256
#define CONSTANTS_FIRST_SLOTS   32768

typedef struct {
    // The vertex and index data of the frame, in the layout of the buffers.
    unsigned char* vertexData;
    unsigned char* indexData;
    WGPUBuffer vertexBuffer;
    WGPUBuffer indexBuffer;
    uint32_t xyz_elements;
    uint32_t color_st_elements;
    uint32_t index_buffer_offset;

    // The vertex shader constants of the frame.
    unsigned char* constantsData;
    WGPUBuffer constantsBuffer;
    WGPUBindGroup constantsBindGroup;
    uint32_t numSlots;
    uint32_t usedSlots;
    uint32_t currentOffset;
    qboolean slotsOverflow;

    const gapi_texture_t* boundTextures[2];
} wgpuGeometry_t;

static wgpuGeometry_t s_geo;


static WGPUBuffer wgpu_createBuffer(const char* label, WGPUBufferUsage usage, uint64_t size)
{
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.label = wgpu_stringView(label);
    desc.usage = usage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    return wgpuDeviceCreateBuffer(wg.device, &desc);
}


static void wgpu_createConstants(uint32_t numSlots)
{
    s_geo.numSlots = numSlots;
    s_geo.constantsData = (unsigned char*) calloc(numSlots, CONSTANTS_SLOT_SIZE);
    if (s_geo.constantsData == NULL)
    {
        ri.Error(ERR_FATAL, "WebGPU: out of memory for the vertex shader constants\n");
    }
    s_geo.constantsBuffer = wgpu_createBuffer("vertex constants", WGPUBufferUsage_Uniform,
            (uint64_t)numSlots * CONSTANTS_SLOT_SIZE);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = s_geo.constantsBuffer;
    entry.offset = 0;
    entry.size = 128;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = wg.uniformLayout;
    desc.entryCount = 1;
    desc.entries = &entry;
    s_geo.constantsBindGroup = wgpuDeviceCreateBindGroup(wg.device, &desc);
}


static void wgpu_releaseConstants(void)
{
    if (s_geo.constantsBindGroup)
        wgpuBindGroupRelease(s_geo.constantsBindGroup);
    if (s_geo.constantsBuffer)
        wgpuBufferRelease(s_geo.constantsBuffer);
    free(s_geo.constantsData);
    s_geo.constantsBindGroup = NULL;
    s_geo.constantsBuffer = NULL;
    s_geo.constantsData = NULL;
}


void wgpu_createGeometryResources(void)
{
    memset(&s_geo, 0, sizeof(s_geo));

    // Group 0: the vertex shader constants (128 bytes: the mvp transform,
    // the eye transform, and the clip plane), with a dynamic offset.
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.hasDynamicOffset = 1;
    entry.buffer.minBindingSize = 128;

    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = wgpu_stringView("vertex constants");
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &entry;
    wg.uniformLayout = wgpuDeviceCreateBindGroupLayout(wg.device, &layoutDesc);

    s_geo.vertexData = (unsigned char*) malloc(VERTEX_BUFFER_SIZE);
    s_geo.indexData = (unsigned char*) malloc(INDEX_BUFFER_SIZE);
    if (s_geo.vertexData == NULL || s_geo.indexData == NULL)
    {
        ri.Error(ERR_FATAL, "WebGPU: out of memory for the geometry\n");
    }
    s_geo.vertexBuffer = wgpu_createBuffer("vertices", WGPUBufferUsage_Vertex, VERTEX_BUFFER_SIZE);
    s_geo.indexBuffer = wgpu_createBuffer("indexes", WGPUBufferUsage_Index, INDEX_BUFFER_SIZE);

    wgpu_createConstants(CONSTANTS_FIRST_SLOTS);
}


void wgpu_releaseGeometryResources(void)
{
    wgpu_releaseConstants();
    if (s_geo.vertexBuffer)
        wgpuBufferRelease(s_geo.vertexBuffer);
    if (s_geo.indexBuffer)
        wgpuBufferRelease(s_geo.indexBuffer);
    free(s_geo.vertexData);
    free(s_geo.indexData);
    memset(&s_geo, 0, sizeof(s_geo));

    if (wg.uniformLayout)
        wgpuBindGroupLayoutRelease(wg.uniformLayout);
    wg.uniformLayout = NULL;
}


void wgpu_beginGeometryFrame(void)
{
    // The last frame had more draws than slots: use twice the slots.
    if (s_geo.slotsOverflow)
    {
        const uint32_t numSlots = s_geo.numSlots * 2;
        ri.Printf(PRINT_DEVELOPER, "WebGPU: %u slots for vertex shader constants\n", numSlots);
        wgpu_releaseConstants();
        wgpu_createConstants(numSlots);
        s_geo.slotsOverflow = qfalse;
    }

    s_geo.xyz_elements = 0;
    s_geo.color_st_elements = 0;
    s_geo.index_buffer_offset = 0;
    s_geo.usedSlots = 0;
    s_geo.currentOffset = 0;
}


void wgpu_writeGeometryFrame(void)
{
    if (s_geo.xyz_elements)
        wgpuQueueWriteBuffer(wg.queue, s_geo.vertexBuffer, XYZ_OFFSET,
                s_geo.vertexData + XYZ_OFFSET, (size_t)s_geo.xyz_elements * sizeof(vec4_t));
    if (s_geo.color_st_elements)
    {
        wgpuQueueWriteBuffer(wg.queue, s_geo.vertexBuffer, COLOR_OFFSET,
                s_geo.vertexData + COLOR_OFFSET, (size_t)s_geo.color_st_elements * 4);
        wgpuQueueWriteBuffer(wg.queue, s_geo.vertexBuffer, ST0_OFFSET,
                s_geo.vertexData + ST0_OFFSET, (size_t)s_geo.color_st_elements * sizeof(vec2_t));
        wgpuQueueWriteBuffer(wg.queue, s_geo.vertexBuffer, ST1_OFFSET,
                s_geo.vertexData + ST1_OFFSET, (size_t)s_geo.color_st_elements * sizeof(vec2_t));
    }
    if (s_geo.index_buffer_offset)
        wgpuQueueWriteBuffer(wg.queue, s_geo.indexBuffer, 0, s_geo.indexData, s_geo.index_buffer_offset);
    if (s_geo.usedSlots)
        wgpuQueueWriteBuffer(wg.queue, s_geo.constantsBuffer, 0, s_geo.constantsData,
                (size_t)s_geo.usedSlots * CONSTANTS_SLOT_SIZE);
}


void gapi_setVertexConstants(const gapi_vertexConstants_t* constants)
{
    // gapi_beginFrame skipped the frame: record no commands.
    if (!wg.frameActive)
        return;

    if (s_geo.usedSlots == s_geo.numSlots)
    {
        // No free slot: draw with the last slot, and have more slots at the
        // next frame.
        if (!s_geo.slotsOverflow)
            ri.Printf(PRINT_DEVELOPER, "WebGPU: no slot for vertex shader constants\n");
        s_geo.slotsOverflow = qtrue;
        s_geo.usedSlots--;
    }

    // The 128 bytes of gapi_vertexConstants_t before the portal flag have the
    // layout of the Transform uniform block. The other floats are not used
    // when the view is not a portal.
    s_geo.currentOffset = s_geo.usedSlots * CONSTANTS_SLOT_SIZE;
    memcpy(s_geo.constantsData + s_geo.currentOffset, constants, 128);
    s_geo.usedSlots++;
}


void gapi_bindTexture(uint32_t tmu, const gapi_texture_t* tex)
{
    s_geo.boundTextures[tmu] = tex;
}


void gapi_uploadPositionsAndIndexes(const vec4_t* xyz, uint32_t numVertexes,
        const uint32_t* indexes, uint32_t numIndexes)
{
    // gapi_beginFrame skipped the frame: record no commands.
    if (!wg.frameActive)
        return;

    const uint64_t xyz_offset = XYZ_OFFSET + (uint64_t)s_geo.xyz_elements * sizeof(vec4_t);
    if ((s_geo.xyz_elements + numVertexes) * sizeof(vec4_t) > XYZ_SIZE)
        ri.Error(ERR_DROP, "WebGPU: vertex buffer overflow (xyz)\n");

    memcpy(s_geo.vertexData + xyz_offset, xyz, (size_t)numVertexes * sizeof(vec4_t));
    wgpuRenderPassEncoderSetVertexBuffer(wg.pass, 0, s_geo.vertexBuffer, xyz_offset,
            (uint64_t)numVertexes * sizeof(vec4_t));
    s_geo.xyz_elements += numVertexes;

    if (numIndexes != 0)
    {
        const uint32_t size = numIndexes * sizeof(uint32_t);
        if (s_geo.index_buffer_offset + size > INDEX_BUFFER_SIZE)
            ri.Error(ERR_DROP, "WebGPU: index buffer overflow\n");

        memcpy(s_geo.indexData + s_geo.index_buffer_offset, indexes, size);
        wgpuRenderPassEncoderSetIndexBuffer(wg.pass, s_geo.indexBuffer, WGPUIndexFormat_Uint32,
                s_geo.index_buffer_offset, size);
        s_geo.index_buffer_offset += size;
    }
}


// The scissor rectangle, clamped to the surface.
static void wgpu_setViewportScissor(const gapi_rect_t* rect, gapi_depthRange_t depthRange)
{
    float minDepth = 0.0f, maxDepth = 1.0f;
    switch (depthRange)
    {
        case GAPI_DEPTH_NORMAL: minDepth = 0.0f; maxDepth = 1.0f; break;
        case GAPI_DEPTH_ZERO:   minDepth = 0.0f; maxDepth = 0.0f; break;
        case GAPI_DEPTH_ONE:    minDepth = 1.0f; maxDepth = 1.0f; break;
        case GAPI_DEPTH_WEAPON: minDepth = 0.0f; maxDepth = 0.3f; break;
    }
    wgpuRenderPassEncoderSetViewport(wg.pass, (float)rect->x, (float)rect->y,
            (float)rect->width, (float)rect->height, minDepth, maxDepth);

    const int x0 = MAX(rect->x, 0);
    const int y0 = MAX(rect->y, 0);
    const int x1 = MIN(rect->x + rect->width, (int)wg.width);
    const int y1 = MIN(rect->y + rect->height, (int)wg.height);
    wgpuRenderPassEncoderSetScissorRect(wg.pass, x0, y0,
            (x1 > x0) ? (uint32_t)(x1 - x0) : 0, (y1 > y0) ? (uint32_t)(y1 - y0) : 0);
}


void gapi_draw(const gapi_pipeline_t* pipeline, const gapi_draw_t* draw)
{
    // gapi_beginFrame skipped the frame: record no commands.
    if (!wg.frameActive)
        return;

    const qboolean multitexture = (draw->texCoords1 != NULL);
    const uint32_t first = s_geo.color_st_elements;

    if ((first + draw->numVertexes) * 4 > COLOR_SIZE)
        ri.Error(ERR_DROP, "WebGPU: vertex buffer overflow (color)\n");

    // The colors and the texture coordinates.
    memcpy(s_geo.vertexData + COLOR_OFFSET + first * 4, draw->colors, (size_t)draw->numVertexes * 4);
    memcpy(s_geo.vertexData + ST0_OFFSET + first * sizeof(vec2_t), draw->texCoords0,
            (size_t)draw->numVertexes * sizeof(vec2_t));
    wgpuRenderPassEncoderSetVertexBuffer(wg.pass, 1, s_geo.vertexBuffer, COLOR_OFFSET + (uint64_t)first * 4,
            (uint64_t)draw->numVertexes * 4);
    wgpuRenderPassEncoderSetVertexBuffer(wg.pass, 2, s_geo.vertexBuffer, ST0_OFFSET + (uint64_t)first * sizeof(vec2_t),
            (uint64_t)draw->numVertexes * sizeof(vec2_t));
    if (multitexture)
    {
        memcpy(s_geo.vertexData + ST1_OFFSET + first * sizeof(vec2_t), draw->texCoords1,
                (size_t)draw->numVertexes * sizeof(vec2_t));
        wgpuRenderPassEncoderSetVertexBuffer(wg.pass, 3, s_geo.vertexBuffer, ST1_OFFSET + (uint64_t)first * sizeof(vec2_t),
                (uint64_t)draw->numVertexes * sizeof(vec2_t));
    }
    s_geo.color_st_elements += draw->numVertexes;

    // r_apiCoverage heavy: the first draw waits for the asynchronous build.
    wgpu_resolvePipeline((gapi_pipeline_t*)pipeline);
    // WebGPU has no dynamic depth bias: the pipeline has the bias.
    wgpu_updatePipelineDepthBias((gapi_pipeline_t*)pipeline, draw->depthBiasConstant, draw->depthBiasSlope);
    wgpuRenderPassEncoderSetPipeline(wg.pass, pipeline->pipeline);

    wgpuRenderPassEncoderSetBindGroup(wg.pass, 0, s_geo.constantsBindGroup, 1, &s_geo.currentOffset);
    wgpuRenderPassEncoderSetBindGroup(wg.pass, 1, s_geo.boundTextures[0]->bindGroup, 0, NULL);
    if (multitexture)
        wgpuRenderPassEncoderSetBindGroup(wg.pass, 2, s_geo.boundTextures[1]->bindGroup, 0, NULL);

    wgpu_setViewportScissor(&draw->viewport, draw->depthRange);

    if (draw->numIndexes != 0)
        wgpuRenderPassEncoderDrawIndexed(wg.pass, draw->numIndexes, 1, 0, 0, 0);
    else
        wgpuRenderPassEncoderDraw(wg.pass, draw->numVertexes, 1, 0, 0);
}


uint64_t wgpu_geometryBufferBytes(uint32_t* bufferCount)
{
    const WGPUBuffer buffers[] = { s_geo.vertexBuffer, s_geo.indexBuffer, s_geo.constantsBuffer };
    uint64_t bytes = 0;
    uint32_t count = 0;
    size_t i;

    for (i = 0; i < ARRAY_LEN(buffers); i++)
    {
        if (buffers[i] == NULL)
            continue;
        // The usage is not necessary for the sum. It is here for the API coverage.
        if (wgpuBufferGetUsage(buffers[i]) == WGPUBufferUsage_None)
            continue;
        bytes += wgpuBufferGetSize(buffers[i]);
        count++;
    }

    *bufferCount = count;
    return bytes;
}


void gapi_pushDebugGroup(const char* name)
{
    if (!wg.frameActive || !wg.heavyCoverage)
        return;
    wgpuRenderPassEncoderPushDebugGroup(wg.pass, wgpu_stringView(name));
}


void gapi_popDebugGroup(void)
{
    if (!wg.frameActive || !wg.heavyCoverage)
        return;
    wgpuRenderPassEncoderPopDebugGroup(wg.pass);
}

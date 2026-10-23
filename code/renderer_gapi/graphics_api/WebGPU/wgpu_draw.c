// Phase 2 placeholders for the textures, the pipelines, and the draws.
// The handles are real, so the shared code works without a change, but
// nothing is drawn yet. Phase 3 replaces these functions.

#include "wgpu_local.h"

static uint32_t s_numTextures = 0;


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

    ++s_numTextures;
    return tex;
}


void gapi_uploadTexture(gapi_texture_t* tex, const unsigned char* rgba, uint32_t numLevels)
{
}


void gapi_destroyTexture(gapi_texture_t* tex)
{
    free(tex);
    --s_numTextures;
}


void gapi_printMemoryInfo(void)
{
    ri.Printf(PRINT_ALL, "Number of textures: %u\n", s_numTextures);
}


gapi_pipeline_t* gapi_createPipeline(const gapi_pipelineDesc_t* desc)
{
    gapi_pipeline_t* pipeline = (gapi_pipeline_t*) malloc(sizeof(gapi_pipeline_t));
    if (pipeline == NULL)
    {
        ri.Error(ERR_FATAL, "gapi_createPipeline: out of memory\n");
    }

    pipeline->desc = *desc;
    return pipeline;
}


void gapi_destroyPipeline(gapi_pipeline_t* pipeline)
{
    free(pipeline);
}


void gapi_setVertexConstants(const gapi_vertexConstants_t* constants)
{
}


void gapi_bindTexture(uint32_t tmu, const gapi_texture_t* tex)
{
}


void gapi_uploadPositionsAndIndexes(const vec4_t* xyz, uint32_t numVertexes,
        const uint32_t* indexes, uint32_t numIndexes)
{
}


void gapi_draw(const gapi_pipeline_t* pipeline, const gapi_draw_t* draw)
{
}


void gapi_clearColor(const gapi_rect_t* rect, const float color[4])
{
}


void gapi_clearDepthStencil(const gapi_rect_t* rect, qboolean stencil)
{
}


void gapi_readPixels(unsigned char* rgba, uint32_t width, uint32_t height)
{
    memset(rgba, 0, (size_t)width * height * 4);
}

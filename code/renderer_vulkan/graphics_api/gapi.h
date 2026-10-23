#ifndef GAPI_H_
#define GAPI_H_

/*
 * The interface between the Quake 3 renderer and a graphics API.
 *
 * The renderer code that all graphics APIs share uses only the types and
 * functions in this file. Each graphics API in graphics_api/<API>/ defines
 * the structs behind the handles and implements the functions.
 *
 * Clip space: the shared code makes its matrices for the Vulkan clip space,
 * with Y down and Z from 0 to 1. A graphics API with a different clip space
 * corrects it in its vertex shaders.
 */

#include "../../qcommon/q_shared.h"

// A texture, its sampler state, and the API objects that bind it to a draw.
typedef struct gapi_texture_s gapi_texture_t;

// A graphics pipeline for one set of Q3 shader stage state.
typedef struct gapi_pipeline_s gapi_pipeline_t;

// The optional features of the graphics API and the device.
typedef struct {
	qboolean fillModeLine;		// pipelines can draw triangles as lines (GLS_POLYMODE_LINE)
} gapi_caps_t;

// The fragment shader of a pipeline.
typedef enum {
	GAPI_SHADER_SINGLE_TEXTURE,
	GAPI_SHADER_MULTI_TEXTURE_MUL,	// texture 0 * texture 1
	GAPI_SHADER_MULTI_TEXTURE_ADD	// texture 0 + texture 1
} gapi_shaderType_t;

// The face culling of a pipeline, in the same order as cullType_t.
typedef enum {
	GAPI_CULL_FRONT_SIDED,
	GAPI_CULL_BACK_SIDED,
	GAPI_CULL_TWO_SIDED
} gapi_cullType_t;

// The stencil shadow phase of a pipeline (cg_shadows 2).
typedef enum {
	GAPI_SHADOW_DISABLED,
	GAPI_SHADOW_EDGES,				// increment or decrement the stencil, no color
	GAPI_SHADOW_FULLSCREEN_QUAD		// darken where the stencil is not 0
} gapi_shadowPhase_t;

// The state of a graphics pipeline.
typedef struct {
	uint32_t stateBits;				// GLS_* bits
	gapi_cullType_t cullType;
	qboolean polygonOffset;			// depth bias from gapi_draw_t
	qboolean clipPlane;				// portal view: clip with the clip plane
	qboolean mirror;				// mirror view: the front face is reversed
	qboolean linePrimitives;		// draw lines, not triangles
	gapi_shaderType_t shaderType;
	gapi_shadowPhase_t shadowPhase;
} gapi_pipelineDesc_t;

// The depth range of a draw, set with the viewport.
typedef enum {
	GAPI_DEPTH_NORMAL,	// [0..1]
	GAPI_DEPTH_ZERO,	// [0..0]
	GAPI_DEPTH_ONE,		// [1..1]
	GAPI_DEPTH_WEAPON	// [0..0.3]
} gapi_depthRange_t;

// A rectangle in framebuffer pixels, with the origin at the top left.
typedef struct {
	int x, y;
	int width, height;
} gapi_rect_t;

// The vertex shader constants of a draw.
typedef struct {
	float mvp[16];				// model view projection transform
	float eyeTransform[12];		// portal views only: model to eye space, three rows
	float clipPlane[4];			// portal views only: clip plane in eye space
	qboolean portal;			// qtrue if eyeTransform and clipPlane are set
} gapi_vertexConstants_t;

// The per-vertex data and the state of a draw. The positions and indexes come
// from the last gapi_uploadPositionsAndIndexes call.
typedef struct {
	const byte (*colors)[4];
	const vec2_t* texCoords0;
	const vec2_t* texCoords1;	// NULL for a draw with one texture
	uint32_t numVertexes;
	uint32_t numIndexes;		// 0 for a draw without indexes
	gapi_depthRange_t depthRange;
	gapi_rect_t viewport;		// also the scissor rectangle
	float depthBiasConstant;	// used only if the pipeline has a polygon offset
	float depthBiasSlope;
} gapi_draw_t;

// Get the optional features. Call this after the graphics API starts.
void gapi_getCaps(gapi_caps_t* caps);

// Wait until the device is idle. Call this before resources that the device
// can use are destroyed.
void gapi_waitIdle(void);

// Create and destroy a graphics pipeline. The shared code keeps the cache.
gapi_pipeline_t* gapi_createPipeline(const gapi_pipelineDesc_t* desc);
void gapi_destroyPipeline(gapi_pipeline_t* pipeline);

// Create a 2D RGBA8 texture with mipLevels levels. mipmap selects the
// filter between levels, and repeat selects the address mode.
gapi_texture_t* gapi_createTexture(uint32_t width, uint32_t height, uint32_t mipLevels,
		qboolean mipmap, qboolean repeat);

// Upload numLevels levels. The levels are packed in rgba, from level 0. Each
// level is half the size of the level before it, and at least 1 pixel.
void gapi_uploadTexture(gapi_texture_t* tex, const unsigned char* rgba, uint32_t numLevels);

void gapi_destroyTexture(gapi_texture_t* tex);

// The console command that prints the texture memory use.
void gapi_printMemoryInfo(void);

// Set the vertex shader constants for the next draws.
void gapi_setVertexConstants(const gapi_vertexConstants_t* constants);

// Bind a texture to texture unit 0 or 1 for the next draws.
void gapi_bindTexture(uint32_t tmu, const gapi_texture_t* tex);

// Give the positions and the indexes for the next draws. Set numIndexes to 0
// for draws without indexes.
void gapi_uploadPositionsAndIndexes(const vec4_t* xyz, uint32_t numVertexes,
		const uint32_t* indexes, uint32_t numIndexes);

// Draw with a pipeline.
void gapi_draw(const gapi_pipeline_t* pipeline, const gapi_draw_t* draw);

// Clear a rectangle of the color attachment.
void gapi_clearColor(const gapi_rect_t* rect, const float color[4]);

// Clear a rectangle of the depth attachment, and of the stencil if stencil is qtrue.
void gapi_clearDepthStencil(const gapi_rect_t* rect, qboolean stencil);

#endif

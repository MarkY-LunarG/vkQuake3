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

struct SDL_Window;

// A texture, its sampler state, and the API objects that bind it to a draw.
typedef struct gapi_texture_s gapi_texture_t;

// A graphics pipeline for one set of Q3 shader stage state.
typedef struct gapi_pipeline_s gapi_pipeline_t;

// The optional features of the graphics API and the device.
typedef struct {
	qboolean fillModeLine;		// pipelines can draw triangles as lines (GLS_POLYMODE_LINE)
	qboolean occlusionQueries;	// gapi_beginOcclusionQuery and the other occlusion query functions work
	char vendor[256];			// for glConfig.vendor_string
	char renderer[256];			// for glConfig.renderer_string
	char version[256];			// for glConfig.version_string
	char extensions[BIG_INFO_STRING];	// for glConfig.extensions_string
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

// The SDL window flags that the graphics API needs, for example SDL_WINDOW_VULKAN.
uint32_t gapi_sdlWindowFlags(void);

// Start the graphics API for a window that R_CreateWindow made with
// gapi_sdlWindowFlags. Return qfalse if it cannot start on this window.
qboolean gapi_init(struct SDL_Window* window);
void gapi_shutdown(void);
qboolean gapi_isInitialized(void);

// r_swapInterval: 0 is no vertical sync, 1 is vertical sync, -1 is adaptive
// vertical sync. A change takes effect at the next frame.
void gapi_setSwapInterval(int interval);

// Start and end a frame. gapi_endFrame submits the frame and presents it.
void gapi_beginFrame(void);
void gapi_endFrame(void);

// The console command that prints information about the graphics API and the device.
void gapi_printInfo(void);

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

// Give the image name to the texture, for debug and capture tools. The WebGPU
// backend sets the label only with r_apiCoverage heavy. The Vulkan backend does
// not use the name.
void gapi_setTextureName(gapi_texture_t* tex, const char* name);

// The console command that prints the texture memory use.
void gapi_printMemoryInfo(void);

// Read the pixels of the frame: width * height RGBA pixels, the top row first.
void gapi_readPixels(unsigned char* rgba, uint32_t width, uint32_t height);

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

// Start and end a named group of draws, for debug and capture tools. The
// WebGPU backend records the groups only with r_apiCoverage heavy. The Vulkan
// backend does not record them.
void gapi_pushDebugGroup(const char* name);
void gapi_popDebugGroup(void);
// Put a named marker between the draws, for debug and capture tools. The same
// rules as for the debug groups apply.
void gapi_insertDebugMarker(const char* name);

// Occlusion queries, if gapi_caps_t.occlusionQueries is qtrue. A query counts
// the samples of the draws between gapi_beginOcclusionQuery and
// gapi_endOcclusionQuery that pass the depth test. Use each index only one time
// in a frame. The result comes back some frames later, with the tag of the
// query: gapi_getOcclusionResult gives the newest result of an index only one
// time, and returns qfalse when there is no new result.
#define GAPI_MAX_OCCLUSION_QUERIES 256
void gapi_beginOcclusionQuery(uint32_t index, uint32_t tag);
void gapi_endOcclusionQuery(void);
qboolean gapi_getOcclusionResult(uint32_t index, uint32_t* tag, uint64_t* samples);

#endif

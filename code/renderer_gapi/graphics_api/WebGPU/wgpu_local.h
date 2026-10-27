#ifndef WGPU_LOCAL_H_
#define WGPU_LOCAL_H_

#include <webgpu/webgpu.h>

#include "graphics_api/gapi.h"
#include "ref_import.h"

// The WebGPU backend state. gapi_init makes it, and gapi_shutdown clears it.
typedef struct {
	qboolean initialized;

	WGPUInstance instance;
	WGPUSurface surface;
	WGPUAdapter adapter;
	WGPUDevice device;
	WGPUQueue queue;

	// The surface configuration.
	WGPUTextureFormat surfaceFormat;
	WGPUTextureUsage surfaceUsage;
	WGPUPresentMode presentMode;
	uint32_t width, height;
	qboolean configured;
	qboolean reconfigure;		// configure the surface again before the next frame

	// Without the CopySrc usage, screenshots cannot copy the surface texture.
	// The frame is then drawn into this texture, and gapi_endFrame copies it to
	// the surface. r_webgpuOffscreen 1 uses this path on all surfaces (tests).
	qboolean offscreen;
	WGPUTexture offscreenTexture;
	WGPUTextureView offscreenView;
	cvar_t* forceOffscreen;

	// The depth and stencil attachment, with the size of the surface.
	WGPUTexture depthTexture;
	WGPUTextureView depthView;

	// The current frame.
	qboolean frameActive;		// gapi_beginFrame started a frame
	WGPUTexture frameTexture;
	WGPUTextureView frameView;
	WGPUCommandEncoder encoder;
	WGPURenderPassEncoder pass;

	qboolean deviceLost;

	// The number of uncaptured errors. With r_webgpuFatalErrors 1, the next
	// frame stops the game with a fatal error. Tests use this.
	uint32_t errorCount;
	cvar_t* fatalErrors;

	// The device has the clip-distances feature.
	qboolean clipDistances;

	// r_apiCoverage heavy: use more of the WebGPU API than the default path, for
	// captures that test the API coverage of a capture tool. The default path does
	// not change. vid_restart applies a change (CVAR_LATCH).
	cvar_t* apiCoverage;
	qboolean heavyCoverage;
	WGPUFuture lostFuture;			// heavy: the device-lost future
	qboolean timestampQuery;		// heavy: the device has the timestamp-query feature

	// The objects that exist from gapi_init to gapi_shutdown.
	WGPUBindGroupLayout uniformLayout;		// group 0: the vertex shader constants
	WGPUBindGroupLayout textureLayout;		// groups 1 and 2: a texture and its sampler
	WGPUPipelineLayout layoutSingle;		// groups 0 and 1
	WGPUPipelineLayout layoutMulti;			// groups 0, 1, and 2
	WGPUSampler samplers[2][2];				// [mipmap][repeat]
} wgpuState_t;

extern wgpuState_t wg;

// The WebGPU objects behind the graphics API handles.
struct gapi_texture_s {
	uint32_t width, height;
	uint32_t mipLevels;
	qboolean mipmap;
	qboolean repeat;

	WGPUTexture texture;
	WGPUTextureView view;
	WGPUBindGroup bindGroup;				// the texture and its sampler

	// The list of live textures, for gpuMem.
	struct gapi_texture_s* prev;
	struct gapi_texture_s* next;
};

struct gapi_pipeline_s {
	gapi_pipelineDesc_t desc;
	WGPURenderPipeline pipeline;
	// The depth bias of the pipeline. WebGPU has no dynamic depth bias, so
	// the pipeline is made again when gapi_draw_t gives other values.
	float depthBiasConstant;
	float depthBiasSlope;

	// r_apiCoverage heavy: the first build is asynchronous. pipeline is NULL
	// until wgpu_resolvePipeline waits for the future.
	qboolean pending;
	WGPUFuture future;
	struct gapi_pipeline_s* pendingPrev;	// the list of pending builds
	struct gapi_pipeline_s* pendingNext;
};

// wgpu_surface_sdl.c: make the surface for the window.
WGPUSurface wgpu_createSurfaceImpl(WGPUInstance instance, struct SDL_Window* window);

// wgpu_frame.c: configure the surface and make the depth attachment.
qboolean wgpu_configureSurface(void);
void wgpu_releaseFrameResources(void);

// wgpu_frame.c, r_apiCoverage heavy: the timestamp queries of the render pass.
// They do nothing without the timestamp-query feature.
void wgpu_createTimingResources(void);
void wgpu_releaseTimingResources(void);
void wgpu_printTimingInfo(void);
// wgpu_frame.c, r_apiCoverage heavy: the occlusion queries (gapi_beginOcclusionQuery).
void wgpu_createOcclusionResources(void);
void wgpu_releaseOcclusionResources(void);

// wgpu_texture.c: the samplers and the texture bind group layout.
void wgpu_createTextureResources(void);
void wgpu_releaseTextureResources(void);
// r_apiCoverage heavy: the GPU makes the mipmaps (gapi_generateMipmaps).
qboolean wgpu_mipmapGeneration(void);

// wgpu_pipeline.c: the shader modules and the pipeline layouts.
void wgpu_createPipelineResources(void);
void wgpu_releasePipelineResources(void);
// Make the WebGPU pipeline again if the depth bias changed.
void wgpu_updatePipelineDepthBias(gapi_pipeline_t* pipeline, float constant, float slope);
// Wait for an asynchronous pipeline build (r_apiCoverage heavy). Call it before
// the first use of the pipeline.
void wgpu_resolvePipeline(gapi_pipeline_t* pipeline);
// Wait for all the asynchronous pipeline builds. gapi_beginFrame calls it before
// it gets the surface texture: a browser destroys the surface texture of a frame
// when a wait in the frame gives control back to the browser.
void wgpu_resolvePendingPipelines(void);

// wgpu_geometry.c: the vertex shader constants and the geometry buffers.
void wgpu_createGeometryResources(void);
void wgpu_releaseGeometryResources(void);
// Start a frame: reset the buffers. Write the data of the frame to the GPU
// before the submit.
void wgpu_beginGeometryFrame(void);
void wgpu_writeGeometryFrame(void);
// gpuMem with r_apiCoverage heavy: the bytes of the geometry buffers.
uint64_t wgpu_geometryBufferBytes(uint32_t* bufferCount);

// wgpu_draw.c: the clear pipelines, and the copy of the last frame for
// gapi_readPixels. Call wgpu_keepLastFrame after the render pass ends.
void wgpu_createDrawResources(void);
void wgpu_releaseDrawResources(void);
void wgpu_keepLastFrame(void);
// Copy the offscreen texture to the surface texture, after the render pass of
// the frame.
void wgpu_blitToSurface(void);

// wgpu_init.c, r_apiCoverage heavy: an error scope around the creation of an
// object. wgpu_popErrorScope does not wait: the next wgpuInstanceProcessEvents
// prints an error with the label. Thus the label must be a static string. With
// the default path, the two functions do nothing.
void wgpu_pushErrorScope(void);
void wgpu_popErrorScope(const char* label);

// A string view from a C string, and a C string from a string view.
WGPUStringView wgpu_stringView(const char* text);
const char* wgpu_cString(WGPUStringView view, char* buffer, size_t size);

#endif

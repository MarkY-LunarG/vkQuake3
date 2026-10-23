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

	// The device has the clip-distances feature.
	qboolean clipDistances;

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
};

struct gapi_pipeline_s {
	gapi_pipelineDesc_t desc;
	WGPURenderPipeline pipeline;
	// The depth bias of the pipeline. WebGPU has no dynamic depth bias, so
	// the pipeline is made again when gapi_draw_t gives other values.
	float depthBiasConstant;
	float depthBiasSlope;
};

// wgpu_surface_sdl.c: make the surface for the window.
WGPUSurface wgpu_createSurfaceImpl(WGPUInstance instance, struct SDL_Window* window);

// wgpu_frame.c: configure the surface and make the depth attachment.
qboolean wgpu_configureSurface(void);
void wgpu_releaseFrameResources(void);

// wgpu_texture.c: the samplers and the texture bind group layout.
void wgpu_createTextureResources(void);
void wgpu_releaseTextureResources(void);

// wgpu_pipeline.c: the shader modules and the pipeline layouts.
void wgpu_createPipelineResources(void);
void wgpu_releasePipelineResources(void);
// Make the WebGPU pipeline again if the depth bias changed.
void wgpu_updatePipelineDepthBias(gapi_pipeline_t* pipeline, float constant, float slope);

// wgpu_geometry.c: the vertex shader constants and the geometry buffers.
void wgpu_createGeometryResources(void);
void wgpu_releaseGeometryResources(void);
// Start a frame: reset the buffers. Write the data of the frame to the GPU
// before the submit.
void wgpu_beginGeometryFrame(void);
void wgpu_writeGeometryFrame(void);

// wgpu_draw.c: the clear pipelines, and the copy of the last frame for
// gapi_readPixels. Call wgpu_keepLastFrame after the render pass ends.
void wgpu_createDrawResources(void);
void wgpu_releaseDrawResources(void);
void wgpu_keepLastFrame(void);

// A string view from a C string, and a C string from a string view.
WGPUStringView wgpu_stringView(const char* text);
const char* wgpu_cString(WGPUStringView view, char* buffer, size_t size);

#endif

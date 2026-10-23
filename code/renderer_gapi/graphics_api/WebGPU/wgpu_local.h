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
} wgpuState_t;

extern wgpuState_t wg;

// The WebGPU objects behind the graphics API handles. Phase 3 fills them.
struct gapi_texture_s {
	uint32_t width, height;
	uint32_t mipLevels;
	qboolean mipmap;
	qboolean repeat;
};

struct gapi_pipeline_s {
	gapi_pipelineDesc_t desc;
};

// wgpu_surface_sdl.c: make the surface for the window.
WGPUSurface wgpu_createSurfaceImpl(WGPUInstance instance, struct SDL_Window* window);

// wgpu_frame.c: configure the surface and make the depth attachment.
qboolean wgpu_configureSurface(void);
void wgpu_releaseFrameResources(void);

// A string view from a C string, and a C string from a string view.
WGPUStringView wgpu_stringView(const char* text);
const char* wgpu_cString(WGPUStringView view, char* buffer, size_t size);

#endif

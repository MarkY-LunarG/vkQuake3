#ifndef VKIMPL_H_
#define VKIMPL_H_


/*
====================================================================

IMPLEMENTATION SPECIFIC FUNCTIONS

====================================================================
*/


#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"
#include "graphics_api/gapi.h"

struct SDL_Window;
// Keep the window that the surface is made for.
void vk_setWindowImpl(struct SDL_Window* window);

void vk_getInstanceProcAddrImpl(void);

qboolean vk_createSurfaceImpl(void);
// For tests: qtrue if the cvar r_debugFailGapiInit is the name of the
// current SDL video driver. Then gapi_init fails after the device exists.
qboolean vk_debugFailInitImpl(void);
// Get the instance extensions that the window system needs for a surface.
// If ppNames is NULL, only *pCount is set.
qboolean vk_getInstanceExtensionsImpl(uint32_t* pCount, const char** ppNames);


#endif

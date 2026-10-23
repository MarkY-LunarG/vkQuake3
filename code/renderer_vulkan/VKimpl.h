#ifndef VKIMPL_H_
#define VKIMPL_H_


/*
====================================================================

IMPLEMENTATION SPECIFIC FUNCTIONS

====================================================================
*/


#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

void vk_createWindow(void);
void vk_destroyWindow(void);

void vk_getInstanceProcAddrImpl(void);

void vk_createSurfaceImpl(void);
// Get the instance extensions that the window system needs for a surface.
// If ppNames is NULL, only *pCount is set.
void vk_getInstanceExtensionsImpl(uint32_t* pCount, const char** ppNames);

void vk_minimizeWindow( void );

#endif

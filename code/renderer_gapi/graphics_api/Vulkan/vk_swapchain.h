#ifndef VK_SWAPCHAIN_H_
#define VK_SWAPCHAIN_H_

#include "VKimpl.h"

// Ask for a new swapchain before the next frame.
void vk_requestSwapchainRebuild(void);
// Ask for a new surface and swapchain before the next frame.
void vk_requestSurfaceRecreate(void);
// Make the surface and the swapchain again if a request is pending.
// Return qfalse if the frame must be skipped.
qboolean vk_prepareSwapchain(void);
// Make the swapchain again now. Return qfalse if the frame must be skipped.
qboolean vk_rebuildSwapchainNow(void);
void vk_destroySwapchain(void);
void vk_createSwapChain(VkDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format);


#endif

#ifndef VK_FRAME_H_
#define VK_FRAME_H_

#include "VKimpl.h"



void vk_createRenderPass(void);
void vk_destroyRenderPass(void);
// The framebuffers have the size of the swapchain (vk.swapchain_extent).
void vk_createFrameBuffers(void);
// Destroy the framebuffers and the swapchain image views.
void vk_destroyFrameBuffers(void);
// qfalse when gapi_beginFrame skipped the frame.
qboolean vk_frameActive(void);

void vk_createSwapChain(VkDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format);

void vk_create_sync_primitives(void);
void vk_destroy_sync_primitives(void);

#endif

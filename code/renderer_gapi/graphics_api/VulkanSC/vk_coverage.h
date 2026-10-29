#ifndef VK_COVERAGE_H_
#define VK_COVERAGE_H_

// r_apiCoverage heavy: the renderer uses more of the Vulkan SC API, so that
// a capture tool records more entry points. The images do not change.
//  - GPU frame time: timestamp queries (vkCmdWriteTimestamp2KHR), reset from
//    the host (vkResetQueryPool), and copied into a buffer
//    (vkCmdCopyQueryPoolResults) after the frame number (vkCmdUpdateBuffer).
//  - GPU progress: an event that the frame sets at its end
//    (vkCmdSetEvent2KHR), and that the next frame examines (vkGetEventStatus).
//  - Timeline semaphores from the host: the frame timeline is examined before
//    the wait (vkGetSemaphoreCounterValue), and a texture upload waits for a
//    timeline value that the host signals (vkSignalSemaphore). This feature
//    ("upload") is off unless QVKSC_HEAVY_ONLY=upload: its capture does not
//    replay with Premium-GFXR yet.
//  - Memory binds with vkBindBufferMemory2 and vkBindImageMemory2.
//  - Screenshots: a blit converts the swapchain image to RGBA
//    (vkCmdBlitImage2KHR).
//  - The depth image is cleared with vkCmdClearDepthStencilImage when it is
//    made.
// All objects and memory of these features are made at startup.

#include "VKimpl.h"

// Read r_apiCoverage. Call it before the first object is made.
void vk_coverageReadCvar(void);
qboolean vk_coverageHeavy(void);
// One feature of heavy is on (the debug aid QVKSC_HEAVY_ONLY can turn off the others)
qboolean vk_coverageFeature(const char* feature);

// The objects of the features above, made after the device and the command pool
void vk_coverageCreate(void);
void vk_coverageDestroy(void);

// At the start of a frame, after the wait for the previous frame
void vk_coverageBeginFrame(void);
// After vkBeginCommandBuffer of the frame, outside the render pass
void vk_coverageBeginCommands(VkCommandBuffer cmdBuf);
// After the render pass of the frame
void vk_coverageEndCommands(VkCommandBuffer cmdBuf);

// Bind memory with vkBind*Memory2 when heavy, and with vkBind*Memory if not.
void vk_bindBufferMemory(VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset);
void vk_bindImageMemory(VkImage image, VkDeviceMemory memory, VkDeviceSize offset);

// Submit a texture upload. When heavy, the host signals a timeline value, and
// the submit waits for it.
void vk_submitUpload(VkCommandBuffer cmdBuf);

// When heavy: the RGBA image that a screenshot blits the swapchain image into
VkImage vk_coverageScreenshotImage(void);

#endif

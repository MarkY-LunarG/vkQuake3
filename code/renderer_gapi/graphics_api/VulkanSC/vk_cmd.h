#ifndef VK_CMD_H_
#define VK_CMD_H_

// Vulkan SC: the command buffer memory that a command pool reserves, and the
// most command buffers that are allocated from it at one time.
#define VK_COMMAND_POOL_RESERVED_SIZE       (8 * 1024 * 1024)
#define VK_COMMAND_POOL_MAX_COMMAND_BUFFERS 4

#include "VKimpl.h"


void vk_create_command_pool(VkCommandPool* pPool);
void vk_create_command_buffer(VkCommandPool pool, VkCommandBuffer* pBuf);
void vk_destroy_commands(void);

void record_image_layout_transition(VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags image_aspect_flags,
	VkAccessFlags src_access_flags, VkImageLayout old_layout, VkAccessFlags dst_access_flags, VkImageLayout new_layout);


// A buffer barrier with VK_KHR_synchronization2.
void vk_cmdBufferBarrier(VkCommandBuffer cmdBuf, VkBuffer buffer,
        VkPipelineStageFlags2KHR src_stage, VkAccessFlags2KHR src_access,
        VkPipelineStageFlags2KHR dst_stage, VkAccessFlags2KHR dst_access);

// Submit one command buffer with vkQueueSubmit2KHR. It waits for the binary
// semaphore wait_binary, if it is not VK_NULL_HANDLE. It signals the binary
// semaphore signal_binary and the timeline semaphore signal_timeline (with
// timeline_value), if they are not VK_NULL_HANDLE.
void vk_submit(VkCommandBuffer cmdBuf, VkSemaphore wait_binary, VkPipelineStageFlags2KHR wait_stage,
        VkSemaphore signal_binary, VkSemaphore signal_timeline, uint64_t timeline_value);

#endif

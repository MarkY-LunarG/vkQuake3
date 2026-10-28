#ifndef VK_TEXTURE_H_
#define VK_TEXTURE_H_

#include "VKimpl.h"

uint32_t find_memory_type(uint32_t memory_type_bits, VkMemoryPropertyFlags properties);

// Vulkan SC: the image chunks and the staging buffer, made at startup
void vk_createTextureMemory(void);
void vk_destroyTextureMemory(void);
// Vulkan SC: the buffer that a screenshot reads into, made at startup
void vk_createScreenshotBuffer(uint32_t width, uint32_t height);
void vk_destroyScreenshotBuffer(void);

#endif

#include "tr_local.h"
#include "vk_instance.h"
#include "vk_texture.h"
#include "vk_cmd.h"
#include "ref_import.h"

// Read the pixels of the swapchain image (gapi.h).

/*
Images created with tiling equal to VK_IMAGE_TILING_LINEAR have further restrictions on their
limits and capabilities compared to images created with tiling equal to VK_IMAGE_TILING_OPTIMAL.
Creation of images with tiling VK_IMAGE_TILING_LINEAR may not be supported unless other parameters
meetall of the constraints:
* imageType is VK_IMAGE_TYPE_2D
* format is not a depth/stencil format
* mipLevels is 1
* arrayLayers is 1
* samples is VK_SAMPLE_COUNT_1_BIT
* usage only includes VK_IMAGE_USAGE_TRANSFER_SRC_BIT and/or VK_IMAGE_USAGE_TRANSFER_DST_BIT
Implementations may support additional limits and capabilities beyond those listed above.
*/


// Vulkan SC has no vkFreeMemory: the buffer that a screenshot reads into is
// made at startup, with the size of the swapchain images.
static VkBuffer s_readBuffer = VK_NULL_HANDLE;
static VkDeviceMemory s_readMemory = VK_NULL_HANDLE;
static uint32_t s_readBufferSize = 0;


void vk_createScreenshotBuffer(uint32_t W, uint32_t H)
{
    s_readBufferSize = W * H * 4;

    VkBufferCreateInfo buffer_create_info;
    memset(&buffer_create_info, 0, sizeof(buffer_create_info));
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = s_readBufferSize;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK_CHECK( qvkCreateBuffer(vk.device, &buffer_create_info, NULL, &s_readBuffer) );

    VkMemoryRequirements memory_requirements;
    qvkGetBufferMemoryRequirements(vk.device, s_readBuffer, &memory_requirements);

    VkMemoryAllocateInfo memory_allocate_info;
    memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
    memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_allocate_info.allocationSize = memory_requirements.size;
    memory_allocate_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK( qvkAllocateMemory(vk.device, &memory_allocate_info, NULL, &s_readMemory) );
    VK_CHECK( qvkBindBufferMemory(vk.device, s_readBuffer, s_readMemory, 0) );
}


void vk_destroyScreenshotBuffer(void)
{
    if (s_readBuffer != VK_NULL_HANDLE)
        qvkDestroyBuffer(vk.device, s_readBuffer, NULL);
    s_readBuffer = VK_NULL_HANDLE;
    s_readMemory = VK_NULL_HANDLE;
    s_readBufferSize = 0;
}


static void vk_read_pixels(unsigned char* pBuf, uint32_t W, uint32_t H)
{
	qvkDeviceWaitIdle(vk.device);

    const uint32_t sizeFB = W * H * 4;
    if (sizeFB > s_readBufferSize)
    {
        ri.Error(ERR_DROP, "Vulkan SC: a %ux%u screenshot is larger than the screenshot buffer\n", W, H);
    }
    VkBuffer buffer = s_readBuffer;
    VkDeviceMemory memory = s_readMemory;

    VkBufferImageCopy2KHR image_copy;
    {
        image_copy.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2_KHR;
        image_copy.pNext = NULL;
        image_copy.bufferOffset = 0;
        image_copy.bufferRowLength = W;
        image_copy.bufferImageHeight = H;

        image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_copy.imageSubresource.layerCount = 1;
        image_copy.imageSubresource.mipLevel = 0;
        image_copy.imageSubresource.baseArrayLayer = 0;
        image_copy.imageOffset.x = 0;
        image_copy.imageOffset.y = 0;
        image_copy.imageOffset.z = 0;
        image_copy.imageExtent.width = W;
        image_copy.imageExtent.height = H;
        image_copy.imageExtent.depth = 1;
    }

    // Memory barriers are used to explicitly control access to buffer and image subresource ranges.
    // Memory barriers are used to transfer ownership between queue families, change image layouts,
    // and define availability and visibility operations. They explicitly define the access types 
    // and buffer and image subresource ranges that are included in the access scopes of a memory
    // dependency that is created by a synchronization command that includes them.
    //
    // Image memory barriers only apply to memory accesses involving a specific image subresource
    // range. That is, a memory dependency formed from an image memory barrier is scoped to access
    // via the specified image subresource range. Image memory barriers can also be used to define
    // image layout transitions or a queue family ownership transfer for the specified image 
    // subresource range.

    VkImageMemoryBarrier2KHR image_barrier;
    {
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
        image_barrier.pNext = NULL;
        image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
        image_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT_KHR;
        image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT_KHR;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = vk.swapchain_images_array[vk.idx_swapchain_image];
        image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_barrier.subresourceRange.baseMipLevel = 0;
        image_barrier.subresourceRange.levelCount = 1;
        image_barrier.subresourceRange.baseArrayLayer = 0;
        image_barrier.subresourceRange.layerCount = 1;
    }


    // read pixel with command buffer
    VkCommandBuffer cmdBuf;

    VkCommandBufferAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.pNext = NULL;
    alloc_info.commandPool = vk.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    VK_CHECK(qvkAllocateCommandBuffers(vk.device, &alloc_info, &cmdBuf));

    VkCommandBufferBeginInfo begin_info;
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = NULL;
    VK_CHECK(qvkBeginCommandBuffer(cmdBuf, &begin_info));

    VkDependencyInfoKHR dependency;
    memset(&dependency, 0, sizeof(dependency));
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &image_barrier;
    qvkCmdPipelineBarrier2KHR(cmdBuf, &dependency);

    VkCopyImageToBufferInfo2KHR copy_info;
    copy_info.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2_KHR;
    copy_info.pNext = NULL;
    copy_info.srcImage = vk.swapchain_images_array[vk.idx_swapchain_image];
    copy_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copy_info.dstBuffer = buffer;
    copy_info.regionCount = 1;
    copy_info.pRegions = &image_copy;
    qvkCmdCopyImageToBuffer2KHR(cmdBuf, &copy_info);
    VK_CHECK(qvkEndCommandBuffer(cmdBuf));

    vk_submit(cmdBuf, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, VK_NULL_HANDLE, 0);

    VK_CHECK(qvkQueueWaitIdle(vk.queue));

    qvkFreeCommandBuffers(vk.device, vk.command_pool, 1, &cmdBuf);


    // Memory objects created with the memory property VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    // are considered mappable. Memory objects must be mappable in order to be successfully
    // mapped on the host. 
    //
    // To retrieve a host virtual address pointer to a region of a mappable memory object
    unsigned char* data;
    VK_CHECK(qvkMapMemory(vk.device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&data));
    memcpy(pBuf, data, sizeFB);
    qvkUnmapMemory(vk.device, memory);
}


void gapi_readPixels(unsigned char* rgba, uint32_t width, uint32_t height)
{
    vk_read_pixels(rgba, width, height);

    // The swapchain image has the surface format. Give RGBA.
    if (vk.surface_format.format == VK_FORMAT_B8G8R8A8_UNORM ||
        vk.surface_format.format == VK_FORMAT_B8G8R8A8_SRGB ||
        vk.surface_format.format == VK_FORMAT_B8G8R8A8_SNORM)
    {
        const uint32_t cnPixels = width * height;
        uint32_t i;
        for (i = 0; i < cnPixels; i++)
        {
            const unsigned char b = rgba[i*4];
            rgba[i*4] = rgba[i*4 + 2];
            rgba[i*4 + 2] = b;
        }
    }
}

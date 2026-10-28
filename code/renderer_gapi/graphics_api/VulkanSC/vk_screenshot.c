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


static void vk_read_pixels(unsigned char* pBuf, uint32_t W, uint32_t H)
{

	qvkDeviceWaitIdle(vk.device);

	// Create image in host visible memory to serve as a destination for framebuffer pixels.
  
    const uint32_t sizeFB = W * H * 4;
    

	VkBuffer buffer;
    VkDeviceMemory memory;
    {
        VkBufferCreateInfo buffer_create_info;
        memset(&buffer_create_info, 0, sizeof(buffer_create_info));
        buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size = sizeFB;
        buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VK_CHECK( qvkCreateBuffer(vk.device, &buffer_create_info, NULL, &buffer) );

        VkMemoryRequirements memory_requirements;
        qvkGetBufferMemoryRequirements(vk.device, buffer, &memory_requirements);

        VkMemoryAllocateInfo memory_allocate_info;
        memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
        memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.allocationSize = memory_requirements.size;
        //
        memory_allocate_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VK_CHECK( qvkAllocateMemory(vk.device, &memory_allocate_info, NULL, &memory) );
        VK_CHECK( qvkBindBufferMemory(vk.device, buffer, memory, 0) );
    }


    //////////////////////////////////////////////////////////

    VkBufferImageCopy image_copy;
    {
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

    VkImageMemoryBarrier image_barrier;
    {
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.pNext = NULL;
        image_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
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

    qvkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier); 
    qvkCmdCopyImageToBuffer(cmdBuf, vk.swapchain_images_array[vk.idx_swapchain_image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &image_copy);
    VK_CHECK(qvkEndCommandBuffer(cmdBuf));

    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = NULL;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = NULL;
    submit_info.pWaitDstStageMask = NULL;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmdBuf;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = NULL;
    VK_CHECK(qvkQueueSubmit(vk.queue, 1, &submit_info, VK_NULL_HANDLE));

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
    qvkDestroyBuffer(vk.device, buffer, NULL);
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

#include "tr_local.h"
#include "vk_image_sampler.h"
#include "vk_texture.h"
#include "vk_cmd.h"
#include "vk_instance.h"
#include "vk_gapi_types.h"
#include "ref_import.h"

// The Vulkan side of the textures (gapi.h): device local image memory in
// chunks, a staging buffer for the uploads, and one descriptor set for each
// texture.




#define IMAGE_CHUNK_SIZE        (64 * 1024 * 1024)




struct StagingBuffer_t
{
    // Vulkan supports two primary resource types: buffers and images. 
    // Resources are views of memory with associated formatting and dimensionality.
    // Buffers are essentially unformatted arrays of bytes whereas images contain
    // format information, can be multidimensional and may have associated metadata.
    //
    // Buffers represent linear arrays of data which are used for various purposes
    // by binding them to a graphics or compute pipeline via descriptor sets or via
    // certain commands, or by directly specifying them as parameters to certain commands.
    VkBuffer buff;
    // Host visible memory used to copy image data to device local memory.
    VkDeviceMemory mappableMem;
};

struct ImageChunk_t {
    VkDeviceMemory block;
    uint32_t Used;
    // uint32_t typeIndex;
};


struct deviceLocalMemory_t {
    // One large device device local memory allocation, assigned to multiple images
	struct ImageChunk_t Chunks[8];
	uint32_t Index; // number of chunks used
};

static struct StagingBuffer_t StagBuf;
static uint32_t s_stagingSize = 0;

// The number of textures that exist. When it is 0, the memory is free.
static uint32_t s_numTextures = 0;
static struct deviceLocalMemory_t devMemImg;

void gapi_printMemoryInfo(void)
{
    // approm	 for debug info
    ri.Printf(PRINT_ALL, "Number of image: %d chuck memory(device local) used: %d M \n", 
           s_numTextures, devMemImg.Index * (IMAGE_CHUNK_SIZE>>20) );
}


////////////////////////////////////////

uint32_t find_memory_type(uint32_t memory_type_bits, VkMemoryPropertyFlags properties)
{
    uint32_t i;
    for (i = 0; i < vk.devMemProperties.memoryTypeCount; i++)
    {
        if ( ((memory_type_bits & (1 << i)) != 0) && (vk.devMemProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    ri.Error(ERR_FATAL, "Vulkan: failed to find matching memory type with requested properties");
    return -1;
}


static void vk_createStagingBuffer(uint32_t size)
{

    memset(&StagBuf, 0, sizeof(StagBuf));
    s_stagingSize = size;


    ri.Printf(PRINT_ALL, " Create Staging Buffer: %d\n", size);

    {
        VkBufferCreateInfo buffer_desc;
        buffer_desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_desc.pNext = NULL;
        // flags is a bitmask of VkBufferCreateFlagBits specifying additional parameters of the buffer.
        buffer_desc.flags = 0;
        buffer_desc.size = size;
        // VK_BUFFER_USAGE_TRANSFER_SRC_BIT specifies that the buffer
        // can be used as the source of a transfer command
        buffer_desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        // sharingMode is a VkSharingMode value specifying the sharing mode of the buffer 
        // when it will be accessed by multiple queue families.
        buffer_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        // queueFamilyIndexCount is the number of entries in the pQueueFamilyIndices array.
        buffer_desc.queueFamilyIndexCount = 0;
        // pQueueFamilyIndices is a list of queue families that will access this buffer
        // (ignored if sharingMode is not VK_SHARING_MODE_CONCURRENT).
        buffer_desc.pQueueFamilyIndices = NULL;

        VK_CHECK(qvkCreateBuffer(vk.device, &buffer_desc, NULL, &StagBuf.buff));
    }

    // To determine the memory requirements for a buffer resource

    //  typedef struct VkMemoryRequirements {
    //  VkDeviceSize size;
    //  VkDeviceSize alignment;
    //  uint32_t memoryTypeBits;
    //  } VkMemoryRequirements;

    {
        VkMemoryRequirements memory_requirements;
        qvkGetBufferMemoryRequirements(vk.device, StagBuf.buff, &memory_requirements);

        VkMemoryAllocateInfo alloc_info;
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.pNext = NULL;
        alloc_info.allocationSize = memory_requirements.size;
        alloc_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &StagBuf.mappableMem));

        VK_CHECK(qvkBindBufferMemory(vk.device, StagBuf.buff, StagBuf.mappableMem, 0));

        ri.Printf(PRINT_ALL, " Stagging buffer alignment: %ld, memoryTypeBits: 0x%x, Type Index: %d. \n",
            memory_requirements.alignment, memory_requirements.memoryTypeBits, alloc_info.memoryTypeIndex);
    }
}


static void vk_destroy_staging_buffer(void)
{
    ri.Printf(PRINT_ALL, " Destroy staging buffer. \n");

    if (StagBuf.buff != VK_NULL_HANDLE)
    {
        qvkDestroyBuffer(vk.device, StagBuf.buff, NULL);
        StagBuf.buff = VK_NULL_HANDLE;
    }
    
    if (StagBuf.mappableMem != VK_NULL_HANDLE)
    {
        qvkFreeMemory(vk.device, StagBuf.mappableMem, NULL);
		StagBuf.mappableMem = VK_NULL_HANDLE;
    }

    memset(&StagBuf, 0, sizeof(StagBuf));
    s_stagingSize = 0;
}


static void vk_stagBufferToDeviceLocalMem(VkImage image, VkBufferImageCopy* pRegion, uint32_t num_region)
{
    // An application can copy buffer and image data using several methods
    // depending on the type of data transfer. Data can be copied between
    // buffer objects with vkCmdCopyBuffer and a portion of an image can 
    // be copied to another image with vkCmdCopyImage. 
    //
    // Image data can also be copied to and from buffer memory using
    // vkCmdCopyImageToBuffer and vkCmdCopyBufferToImage.
    //
    // Image data can be blitted (with or without scaling and filtering) 
    // with vkCmdBlitImage. Multisampled images can be resolved to a 
    // non-multisampled image with vkCmdResolveImage.
    //
    VkCommandBuffer cmd_buf;
    {
        VkCommandBufferAllocateInfo alloc_info;
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.pNext = NULL;
        alloc_info.commandPool = vk.command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;
        VK_CHECK(qvkAllocateCommandBuffers(vk.device, &alloc_info, &cmd_buf));

        VkCommandBufferBeginInfo begin_info;
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.pNext = NULL;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        begin_info.pInheritanceInfo = NULL;
        VK_CHECK(qvkBeginCommandBuffer(cmd_buf, &begin_info));
    }


	VkBufferMemoryBarrier barrier;
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.pNext = NULL;
	barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = StagBuf.buff;
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;
    
	qvkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &barrier, 0, NULL);

    record_image_layout_transition(cmd_buf, image, VK_IMAGE_ASPECT_COLOR_BIT,
            0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    
    // To copy data from a buffer object to an image object
    
    // cmd_buf is the command buffer into which the command will be recorded.
    // StagBuf.buff is the source buffer.
    // image is the destination image.
    // dstImageLayout is the layout of the destination image subresources.
    // curLevel is the number of regions to copy.
    // pRegions is a pointer to an array of VkBufferImageCopy structures
    // specifying the regions to copy.
    qvkCmdCopyBufferToImage(cmd_buf, StagBuf.buff, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_region, pRegion);

    record_image_layout_transition(cmd_buf, image,
            VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    
    VK_CHECK(qvkEndCommandBuffer(cmd_buf));

	VkSubmitInfo submit_info;
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;
	submit_info.waitSemaphoreCount = 0;
	submit_info.pWaitSemaphores = NULL;
	submit_info.pWaitDstStageMask = NULL;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmd_buf;
	submit_info.signalSemaphoreCount = 0;
	submit_info.pSignalSemaphores = NULL;

	VK_CHECK(qvkQueueSubmit(vk.queue, 1, &submit_info, VK_NULL_HANDLE));	
    VK_CHECK(qvkQueueWaitIdle(vk.queue));
    
	qvkFreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd_buf);
}


static void vk_createImageAndBindWithMemory(gapi_texture_t* pImg)
{

    VkImageCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    desc.pNext = NULL;
    desc.flags = 0;
    desc.imageType = VK_IMAGE_TYPE_2D;
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    desc.extent.width = pImg->width;
    desc.extent.height = pImg->height;
    desc.extent.depth = 1;
    desc.mipLevels = pImg->mipLevels;
    desc.arrayLayers = 1;
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = NULL;
    desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK(qvkCreateImage(vk.device, &desc, NULL, &pImg->handle));

    // =======================================================
    // Binding it with device local memory
    // =======================================================
    VkMemoryRequirements memory_requirements;
    qvkGetImageMemoryRequirements(vk.device, pImg->handle, &memory_requirements);
    
    // ensure that memory region has proper alignment
    uint32_t mask = (memory_requirements.alignment - 1);


    uint32_t i = 0;
	for (i = 0; i < devMemImg.Index; i++)
    {
		// ensure that memory region has proper alignment
		VkDeviceSize offset_aligned = (devMemImg.Chunks[i].Used + mask) & (~mask);
        VkDeviceSize end = offset_aligned + memory_requirements.size;
		if (end <= IMAGE_CHUNK_SIZE)
        {
            VK_CHECK(qvkBindImageMemory(vk.device, pImg->handle, 
                        devMemImg.Chunks[i].block, offset_aligned));

			devMemImg.Chunks[i].Used = end;
			return;
		}
	}

	// Couldn't find suitable in existing chunk.
    // Allocate a new chunk
    
    VkMemoryAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = NULL;
    alloc_info.allocationSize = IMAGE_CHUNK_SIZE;
    alloc_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory memory;
    VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &memory));
    VK_CHECK(qvkBindImageMemory(vk.device, pImg->handle, memory, 0));

    devMemImg.Chunks[devMemImg.Index].block = memory;
    devMemImg.Chunks[devMemImg.Index].Used = memory_requirements.size;
    ++devMemImg.Index;


    ri.Printf(PRINT_ALL, " --- Device memory allocation --- \n");

    ri.Printf(PRINT_ALL, "alignment: %ld, Type Index: %d. \n",
            memory_requirements.alignment, alloc_info.memoryTypeIndex);
    
    ri.Printf(PRINT_ALL, "Image chuck memory consumed: %d M \n",
            devMemImg.Index * (IMAGE_CHUNK_SIZE >> 20) );

    ri.Printf(PRINT_ALL, " --- ------------------------ --- \n");
}



static void vk_createImageViewAndDescriptorSet(gapi_texture_t* pImage)
{

    VkImageView imageView;

    VkImageViewCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    desc.pNext = NULL;
    desc.flags = 0;
    desc.image = pImage->handle;
    desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    // format is a VkFormat describing the format and type used 
    // to interpret data elements in the image.
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    
    // the components field allows you to swizzle the color channels around
    desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    // The subresourceRange field describes what the image's purpose is
    // and which part of the image should be accessed. 
    //
    // selecting the set of mipmap levels and array layers to be accessible to the view.
    desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    desc.subresourceRange.baseMipLevel = 0;
    desc.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    desc.subresourceRange.baseArrayLayer = 0;
    desc.subresourceRange.layerCount = 1;
    // Image objects are not directly accessed by pipeline shaders for reading or writing image data.
    // Instead, image views representing contiguous ranges of the image subresources and containing
    // additional metadata are used for that purpose. Views must be created on images of compatible
    // types, and must represent a valid subset of image subresources.
    //
    // Some of the image creation parameters are inherited by the view. In particular, image view
    // creation inherits the implicit parameter usage specifying the allowed usages of the image 
    // view that, by default, takes the value of the corresponding usage parameter specified in
    // VkImageCreateInfo at image creation time.
    //
    // This implicit parameter can be overriden by chaining a VkImageViewUsageCreateInfo structure
    // through the pNext member to VkImageViewCreateInfo.
    VK_CHECK(qvkCreateImageView(vk.device, &desc, NULL, &imageView));

    /////  save it just for destroy ???
    pImage->view = imageView;


    ///////////////////////////////////////////////////////
    // create associated descriptor set
    ///////////////////////////////////////////////////////
    // Allocate a descriptor set from the pool. 
    // Note that we have to provide the descriptor set layout that 
    // we defined in the pipeline_layout sample. 
    // This layout describes how the descriptor set is to be allocated.

    VkDescriptorSet desSet;

    VkDescriptorSetAllocateInfo descSetAllocInfo;
    descSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descSetAllocInfo.pNext = NULL;
    descSetAllocInfo.descriptorPool = vk.descriptor_pool;
    descSetAllocInfo.descriptorSetCount = 1;
    descSetAllocInfo.pSetLayouts = &vk.set_layout;

    VK_CHECK(qvkAllocateDescriptorSets(vk.device, &descSetAllocInfo, &desSet));
    
    /////  save it for destroy and update current descriptor
    pImage->descriptor_set = desSet;

    //ri.Printf(PRINT_ALL, " Allocate Descriptor Sets \n");
    VkWriteDescriptorSet descriptor_write;

    VkDescriptorImageInfo image_info;
    image_info.sampler = vk_find_sampler(pImage->mipmap, pImage->repeat);
    image_info.imageView = imageView;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstSet = desSet;
    descriptor_write.dstBinding = 0;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorCount = 1;
    descriptor_write.pNext = NULL;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_write.pImageInfo = &image_info;
    descriptor_write.pBufferInfo = NULL;
    descriptor_write.pTexelBufferView = NULL;
    

    qvkUpdateDescriptorSets(vk.device, 1, &descriptor_write, 0, NULL);

    // The above steps essentially copy the VkDescriptorBufferInfo
    // to the descriptor, which is likely in the device memory.
    //
    // This buffer info includes the handle to the uniform buffer
    // as well as the offset and size of the data that is accessed
    // in the uniform buffer. In this case, the uniform buffer 
    // contains only the MVP transform, so the offset is 0 and 
    // the size is the size of the MVP.
}



static void vk_destroySingleImage( gapi_texture_t* pImg )
{
    if(pImg->descriptor_set != VK_NULL_HANDLE)
    {   
        //To free allocated descriptor sets
        qvkFreeDescriptorSets(vk.device, vk.descriptor_pool, 1, &pImg->descriptor_set);
        pImg->descriptor_set = VK_NULL_HANDLE;
    }

    if (pImg->handle != VK_NULL_HANDLE)
    {
        qvkDestroyImageView(vk.device, pImg->view, NULL);
        qvkDestroyImage(vk.device, pImg->handle, NULL);
        pImg->handle = VK_NULL_HANDLE;
    }
}



gapi_texture_t* gapi_createTexture(uint32_t width, uint32_t height, uint32_t mipLevels,
        qboolean mipmap, qboolean repeat)
{
    gapi_texture_t* tex = (gapi_texture_t*) calloc(1, sizeof(gapi_texture_t));
    if (tex == NULL)
    {
        ri.Error(ERR_FATAL, "gapi_createTexture: out of memory\n");
    }

    tex->width = width;
    tex->height = height;
    tex->mipLevels = mipLevels;
    tex->mipmap = mipmap;
    tex->repeat = repeat;

    vk_createImageAndBindWithMemory(tex);
    vk_createImageViewAndDescriptorSet(tex);

    ++s_numTextures;
    return tex;
}


void gapi_uploadTexture(gapi_texture_t* tex, const unsigned char* rgba, uint32_t numLevels)
{
    // The levels are packed in rgba, from level 0. Each level is half the
    // size of the level before it, and at least 1 pixel.
    VkBufferImageCopy regions[16];
    uint32_t buffer_size = 0;
    uint32_t level_width = tex->width;
    uint32_t level_height = tex->height;
    uint32_t i;

    assert(numLevels <= 16);

    // The set of all bytes bound to all the source regions must not overlap
    // the set of all bytes bound to the destination regions.
    //
    // The set of all bytes bound to each destination region must not overlap
    // the set of all bytes bound to another destination region.
    for (i = 0; i < numLevels; i++)
    {
        regions[i].bufferOffset = buffer_size;
        regions[i].bufferRowLength = 0;
        regions[i].bufferImageHeight = 0;
        regions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[i].imageSubresource.mipLevel = i;
        regions[i].imageSubresource.baseArrayLayer = 0;
        regions[i].imageSubresource.layerCount = 1;
        regions[i].imageOffset.x = 0;
        regions[i].imageOffset.y = 0;
        regions[i].imageOffset.z = 0;
        regions[i].imageExtent.width = level_width;
        regions[i].imageExtent.height = level_height;
        regions[i].imageExtent.depth = 1;

        buffer_size += level_width * level_height * 4;

        level_width >>= 1;
        if (level_width == 0)
            level_width = 1;
        level_height >>= 1;
        if (level_height == 0)
            level_height = 1;
    }

    // Make the staging buffer larger if the upload does not fit.
    if (buffer_size > s_stagingSize)
    {
        uint32_t new_size = (s_stagingSize == 0) ? (8 * 1024 * 1024) : s_stagingSize;
        while (new_size < buffer_size)
            new_size *= 2;
        vk_destroy_staging_buffer();
        vk_createStagingBuffer(new_size);
    }

    void* data;
    VK_CHECK(qvkMapMemory(vk.device, StagBuf.mappableMem, 0, VK_WHOLE_SIZE, 0, &data));
    memcpy(data, rgba, buffer_size);
    qvkUnmapMemory(vk.device, StagBuf.mappableMem);

    vk_stagBufferToDeviceLocalMem(tex->handle, regions, numLevels);
}


void gapi_destroyTexture(gapi_texture_t* tex)
{
    vk_destroySingleImage(tex);
    free(tex);

    if (--s_numTextures > 0)
        return;

    // The last texture is destroyed: free the memory of all textures.
    uint32_t i;

	vk_free_sampler();

    for (i = 0; i < devMemImg.Index; i++)
    {
        qvkFreeMemory(vk.device, devMemImg.Chunks[i].block, NULL);
        devMemImg.Chunks[i].Used = 0;
    }

    devMemImg.Index = 0;


    vk_destroy_staging_buffer();
    // Destroying a pool object implicitly frees all objects allocated from that pool. 
    // Specifically, destroying VkCommandPool frees all VkCommandBuffer objects that 
    // were allocated from it, and destroying VkDescriptorPool frees all 
    // VkDescriptorSet objects that were allocated from it.
    VK_CHECK(qvkResetDescriptorPool(vk.device, vk.descriptor_pool, 0));
}

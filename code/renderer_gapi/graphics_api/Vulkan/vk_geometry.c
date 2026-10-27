#include "vk_geometry.h"
#include "vk_frame.h"
#include "vk_instance.h"
#include "vk_texture.h"
#include "tr_cvar.h"

// The Vulkan side of the geometry and the draws (gapi.h): the vertex and index
// buffers, the descriptor sets of the bound textures, the vertex shader push
// constants, the attachment clears, and the draw commands.



#define VERTEX_CHUNK_SIZE   (768 * 1024)
#define INDEX_BUFFER_SIZE   (2 * 1024 * 1024)

#define XYZ_SIZE            (4 * VERTEX_CHUNK_SIZE)
#define COLOR_SIZE          (1 * VERTEX_CHUNK_SIZE)
#define ST0_SIZE            (2 * VERTEX_CHUNK_SIZE)
#define ST1_SIZE            (2 * VERTEX_CHUNK_SIZE)

#define XYZ_OFFSET          0
#define COLOR_OFFSET        (XYZ_OFFSET + XYZ_SIZE)
#define ST0_OFFSET          (COLOR_OFFSET + COLOR_SIZE)
#define ST1_OFFSET          (ST0_OFFSET + ST0_SIZE)

struct ShadingData_t
{
    // Buffers represent linear arrays of data which are used for various purposes
    // by binding them to a graphics or compute pipeline via descriptor sets or 
    // via certain commands,  or by directly specifying them as parameters to 
    // certain commands. Buffers are represented by VkBuffer handles:
	VkBuffer vertex_buffer;
	unsigned char* vertex_buffer_ptr ; // pointer to mapped vertex buffer
	uint32_t xyz_elements;
	uint32_t color_st_elements;

	VkBuffer index_buffer;
	unsigned char* index_buffer_ptr; // pointer to mapped index buffer
	uint32_t index_buffer_offset;

	// host visible memory that holds both vertex and index data
	VkDeviceMemory vertex_buffer_memory;
	VkDeviceMemory index_buffer_memory;
    VkDescriptorSet curDescriptorSets[2];

    // This flag is used to decide whether framebuffer's depth attachment should be cleared
    // with vmCmdClearAttachment (dirty_depth_attachment == true), or it have just been
    // cleared by render pass instance clear op (dirty_depth_attachment == false).

    VkBool32 s_depth_attachment_dirty;
};

static struct ShadingData_t shadingDat;


VkBuffer vk_getIndexBuffer(void)
{
    return shadingDat.index_buffer;
}


static VkRect2D vk_toRect2D(const gapi_rect_t* rect);

static void vk_setViewportScissor(const gapi_rect_t* rect, gapi_depthRange_t dR,
        VkViewport* const vp, VkRect2D* const pRect)
{
    vp->x = rect->x;
    vp->y = rect->y;
    vp->width = rect->width;
    vp->height = rect->height;
    *pRect = vk_toRect2D(rect);

    switch(dR)
    {
        case GAPI_DEPTH_NORMAL:
        {
        	vp->minDepth = 0.0f;
		    vp->maxDepth = 1.0f;
        }break;

        case GAPI_DEPTH_ZERO:
        {
		    vp->minDepth = 0.0f;
		    vp->maxDepth = 0.0f;
	    }break;
        
        case GAPI_DEPTH_ONE:
        {
		    vp->minDepth = 1.0f;
		    vp->maxDepth = 1.0f;
	    }break;

        case GAPI_DEPTH_WEAPON:
        {
            vp->minDepth = 0.0f;
		    vp->maxDepth = 0.3f;
        }break;
    }
}


// The rectangle, clamped to the swapchain size. The draws and the clears
// must stay in the render area. Normally the rectangle is already inside.
static VkRect2D vk_toRect2D(const gapi_rect_t* rect)
{
    const int x0 = MAX(rect->x, 0);
    const int y0 = MAX(rect->y, 0);
    const int x1 = MIN(rect->x + rect->width, (int)vk.swapchain_extent.width);
    const int y1 = MIN(rect->y + rect->height, (int)vk.swapchain_extent.height);

    VkRect2D r;
    r.offset.x = x0;
    r.offset.y = y0;
    r.extent.width = (x1 > x0) ? (uint32_t)(x1 - x0) : 0;
    r.extent.height = (y1 > y0) ? (uint32_t)(y1 - y0) : 0;
    return r;
}




// Vulkan memory is broken up into two categories, host memory and device memory.
// Host memory is memory needed by the Vulkan implementation for 
// non-device-visible storage. Allocations returned by vkAllocateMemory
// are guaranteed to meet any alignment requirement of the implementation
//
// Host access to buffer must be externally synchronized

void vk_createVertexBuffer(void)
{
    ri.Printf(PRINT_ALL, " Create vertex buffer: shadingDat.vertex_buffer \n");

    VkBufferCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    desc.pNext = NULL;
    desc.flags = 0;
    desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = NULL;
    //VERTEX_BUFFER_SIZE
    desc.size = XYZ_SIZE + COLOR_SIZE + ST0_SIZE + ST1_SIZE;
    desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    
    VK_CHECK(qvkCreateBuffer(vk.device, &desc, NULL, &shadingDat.vertex_buffer));


    VkMemoryRequirements vb_memory_requirements;
    qvkGetBufferMemoryRequirements(vk.device, shadingDat.vertex_buffer, &vb_memory_requirements);
    
    uint32_t memory_type_bits = vb_memory_requirements.memoryTypeBits;

    VkMemoryAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = NULL;
    alloc_info.allocationSize = vb_memory_requirements.size;
    // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT bit specifies that memory allocated with
    // this type can be mapped for host access using vkMapMemory.
    //
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT bit specifies that the host cache
    // management commands vkFlushMappedMemoryRanges and vkInvalidateMappedMemoryRanges
    // are not needed to flush host writes to the device or make device writes visible
    // to the host, respectively.
    alloc_info.memoryTypeIndex = find_memory_type(memory_type_bits, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    ri.Printf(PRINT_ALL, " Allocate device memory for Vertex Buffer: %ld bytes. \n",
            alloc_info.allocationSize);

    VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &shadingDat.vertex_buffer_memory));

    qvkBindBufferMemory(vk.device, shadingDat.vertex_buffer, shadingDat.vertex_buffer_memory, 0);

    void* data;
    VK_CHECK(qvkMapMemory(vk.device, shadingDat.vertex_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data));
    shadingDat.vertex_buffer_ptr = (unsigned char*)data;

}



void vk_createIndexBuffer(void)
{
    ri.Printf(PRINT_ALL, " Create index buffer: shadingDat.index_buffer \n");

    VkBufferCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    desc.pNext = NULL;
    desc.flags = 0;
    desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = NULL;
    desc.size = INDEX_BUFFER_SIZE;
    desc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    VK_CHECK(qvkCreateBuffer(vk.device, &desc, NULL, &shadingDat.index_buffer));

    
    VkMemoryRequirements ib_memory_requirements;
    qvkGetBufferMemoryRequirements(vk.device, shadingDat.index_buffer, &ib_memory_requirements);

    uint32_t memory_type_bits = ib_memory_requirements.memoryTypeBits;


    VkMemoryAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = NULL;
    alloc_info.allocationSize = ib_memory_requirements.size;
    // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT bit specifies that memory allocated with
    // this type can be mapped for host access using vkMapMemory.
    //
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT bit specifies that the host cache
    // management commands vkFlushMappedMemoryRanges and vkInvalidateMappedMemoryRanges
    // are not needed to flush host writes to the device or make device writes visible
    // to the host, respectively.
    alloc_info.memoryTypeIndex = find_memory_type(memory_type_bits, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    ri.Printf(PRINT_ALL, " Allocate device memory for Index Buffer: %ld bytes. \n",
            alloc_info.allocationSize);

    VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &shadingDat.index_buffer_memory));
    qvkBindBufferMemory(vk.device, shadingDat.index_buffer, shadingDat.index_buffer_memory, 0);

    void* data;
    VK_CHECK(qvkMapMemory(vk.device, shadingDat.index_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data));
    shadingDat.index_buffer_ptr = (unsigned char*)data;
}




// Descriptors and Descriptor Sets
// A descriptor is a special opaque shader variable that shaders use to access buffer 
// and image resources in an indirect fashion. It can be thought of as a "pointer" to
// a resource.  The Vulkan API allows these variables to be changed between draw
// operations so that the shaders can access different resources for each draw.

// A descriptor set is called a "set" because it can refer to an array of homogenous
// resources that can be described with the same layout binding. one possible way to
// use multiple descriptors is to construct a descriptor set with two descriptors, 
// with each descriptor referencing a separate texture. Both textures are therefore
// available during a draw. A command in a command buffer could then select the texture
// to use by specifying the index of the desired texture. To describe a descriptor set,
// you use a descriptor set layout.

// Descriptor sets corresponding to bound texture images.

// outside of TR since it shouldn't be cleared during ref re-init
// the renderer front end should never modify glstate_t
//typedef struct {





void gapi_bindTexture(uint32_t tmu, const gapi_texture_t* tex)
{
    shadingDat.curDescriptorSets[tmu] = tex->descriptor_set;
}


void gapi_draw(const gapi_pipeline_t* pipeline, const gapi_draw_t* draw)
{
    // gapi_beginFrame skipped the frame: record no commands.
    if (!vk_frameActive())
        return;

    const qboolean multitexture = (draw->texCoords1 != NULL);

	// configure vertex data stream
	VkBuffer bufs[3] = { shadingDat.vertex_buffer, shadingDat.vertex_buffer, shadingDat.vertex_buffer };
	VkDeviceSize offs[3] = {
		COLOR_OFFSET + shadingDat.color_st_elements * sizeof(draw->colors[0]),
		ST0_OFFSET   + shadingDat.color_st_elements * sizeof(vec2_t),
		ST1_OFFSET   + shadingDat.color_st_elements * sizeof(vec2_t)
	};

    // color
    if ((shadingDat.color_st_elements + draw->numVertexes) * sizeof(draw->colors[0]) > COLOR_SIZE)
        ri.Error(ERR_DROP, "vulkan: vertex buffer overflow (color) %ld \n", 
                (shadingDat.color_st_elements + draw->numVertexes) * sizeof(draw->colors[0]));

    unsigned char* dst_color = shadingDat.vertex_buffer_ptr + offs[0];
    memcpy(dst_color, draw->colors, draw->numVertexes * sizeof(draw->colors[0]));
    // st0

    unsigned char* dst_st0 = shadingDat.vertex_buffer_ptr + offs[1];
    memcpy(dst_st0, draw->texCoords0, draw->numVertexes * sizeof(vec2_t));

	// st1
	if (multitexture)
    {
		unsigned char* dst = shadingDat.vertex_buffer_ptr + offs[2];
		memcpy(dst, draw->texCoords1, draw->numVertexes * sizeof(vec2_t));
	}

	qvkCmdBindVertexBuffers(vk.command_buffer, 1, multitexture ? 3 : 2, bufs, offs);
	shadingDat.color_st_elements += draw->numVertexes;

	// bind descriptor sets

//    vkCmdBindDescriptorSets causes the sets numbered [firstSet.. firstSet+descriptorSetCount-1] to use
//    the bindings stored in pDescriptorSets[0..descriptorSetCount-1] for subsequent rendering commands 
//    (either compute or graphics, according to the pipelineBindPoint).
//    Any bindings that were previously applied via these sets are no longer valid.

	qvkCmdBindDescriptorSets(vk.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
        vk.pipeline_layout, 0, (multitexture ? 2 : 1), shadingDat.curDescriptorSets, 0, NULL);

    // bind pipeline
	qvkCmdBindPipeline(vk.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

	// configure pipeline's dynamic state

    VkViewport viewport;
    VkRect2D scissor;

    vk_setViewportScissor(&draw->viewport, draw->depthRange, &viewport, &scissor);

    qvkCmdSetScissor(vk.command_buffer, 0, 1, &scissor);
    qvkCmdSetViewport(vk.command_buffer, 0, 1, &viewport);


	if (pipeline->polygonOffset) {
		qvkCmdSetDepthBias(vk.command_buffer, draw->depthBiasConstant, 0.0f, draw->depthBiasSlope);
	}

	// issue draw call
	if (draw->numIndexes != 0)
		qvkCmdDrawIndexed(vk.command_buffer, draw->numIndexes, 1, 0, 0, 0);
	else
		qvkCmdDraw(vk.command_buffer, draw->numVertexes, 1, 0, 0);
	
    shadingDat.s_depth_attachment_dirty = VK_TRUE;
}


void gapi_setVertexConstants(const gapi_vertexConstants_t* constants)
{
    // gapi_beginFrame skipped the frame: record no commands.
    if (!vk_frameActive())
        return;

    // As described above in section Pipeline Layouts, the pipeline layout defines shader push constants
    // which are updated via Vulkan commands rather than via writes to memory or copy commands.
    // Push constants represent a high speed path to modify constant data in pipelines
    // that is expected to outperform memory-backed resource updates.
    //
    // 64 bytes: mvp transform. 128 bytes: mvp transform, eye transform, and clipping plane.
    // The fields of gapi_vertexConstants_t have the same layout as the push constants.
    qvkCmdPushConstants(vk.command_buffer, vk.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
            0, constants->portal ? 128 : 64, constants);
}




// =========================================================
// Vertex fetching is controlled via configurable state, 
// as a logically distinct graphics pipeline stage.
//  
//  Vertex Attributes
//
//  Vertex shaders can define input variables, which receive vertex attribute data
//  transferred from one or more VkBuffer(s) by drawing commands. Vertex shader 
//  input variables are bound to buffers via an indirect binding where the vertex 
//  shader associates a vertex input attribute number with each variable, vertex 
//  input attributes are associated to vertex input bindings on a per-pipeline basis, 
//  and vertex input bindings are associated with specific buffers on a per-draw basis
//  via the vkCmdBindVertexBuffers command. 
//
//  Vertex input attribute and vertex input binding descriptions also
//  contain format information controlling how data is extracted from
//  buffer memory and converted to the format expected by the vertex shader.
//
//  There are VkPhysicalDeviceLimits::maxVertexInputAttributes number of vertex
//  input attributes and VkPhysicalDeviceLimits::maxVertexInputBindings number of
//  vertex input bindings (each referred to by zero-based indices), where there 
//  are at least as many vertex input attributes as there are vertex input bindings.
//  Applications can store multiple vertex input attributes interleaved in a single 
//  buffer, and use a single vertex input binding to access those attributes.
//
//  In GLSL, vertex shaders associate input variables with a vertex input attribute
//  number using the location layout qualifier. The component layout qualifier
//  associates components of a vertex shader input variable with components of
//  a vertex input attribute.



void gapi_uploadPositionsAndIndexes(const vec4_t* pXYZ, uint32_t nVertex,
		const uint32_t* pIdx, uint32_t nIndex)
{
	// gapi_beginFrame skipped the frame: record no commands.
	if (!vk_frameActive())
		return;

	// xyz stream
	{
        const VkDeviceSize xyz_offset = XYZ_OFFSET + shadingDat.xyz_elements * sizeof(vec4_t);
		
        unsigned char* vDst = shadingDat.vertex_buffer_ptr + xyz_offset;

        // 4 float in the array, with each 4 bytes.
		memcpy(vDst, pXYZ, nVertex * 16);

		qvkCmdBindVertexBuffers(vk.command_buffer, 0, 1, &shadingDat.vertex_buffer, &xyz_offset);
		
        shadingDat.xyz_elements += nVertex;

        assert (shadingDat.xyz_elements * sizeof(vec4_t) < XYZ_SIZE);
	}

	// indexes stream
    if(nIndex != 0)
	{
		const uint32_t indexes_size = nIndex * sizeof(uint32_t);        

		unsigned char* iDst = shadingDat.index_buffer_ptr + shadingDat.index_buffer_offset;
		memcpy(iDst, pIdx, indexes_size);

		qvkCmdBindIndexBuffer(vk.command_buffer, shadingDat.index_buffer, shadingDat.index_buffer_offset, VK_INDEX_TYPE_UINT32);
		
        shadingDat.index_buffer_offset += indexes_size;

        assert (shadingDat.index_buffer_offset < INDEX_BUFFER_SIZE);
	}
}


void vk_resetGeometryBuffer(void)
{
	// Reset geometry buffer's current offsets.
	shadingDat.xyz_elements = 0;
	shadingDat.color_st_elements = 0;
	shadingDat.index_buffer_offset = 0;
    shadingDat.s_depth_attachment_dirty = VK_FALSE;
}


void vk_destroy_shading_data(void)
{
    ri.Printf(PRINT_ALL, " Destroy vertex/index buffer: shadingDat.vertex_buffer shadingDat.index_buffer. \n");
    ri.Printf(PRINT_ALL, " Free device memory: vertex_buffer_memory index_buffer_memory. \n");

    qvkUnmapMemory(vk.device, shadingDat.vertex_buffer_memory);
	qvkFreeMemory(vk.device, shadingDat.vertex_buffer_memory, NULL);

    qvkUnmapMemory(vk.device, shadingDat.index_buffer_memory);
	qvkFreeMemory(vk.device, shadingDat.index_buffer_memory, NULL);

    qvkDestroyBuffer(vk.device, shadingDat.vertex_buffer, NULL);
	qvkDestroyBuffer(vk.device, shadingDat.index_buffer, NULL);

    memset(&shadingDat, 0, sizeof(shadingDat));


    VK_CHECK(qvkResetDescriptorPool(vk.device, vk.descriptor_pool, 0));
}


void gapi_clearDepthStencil(const gapi_rect_t* rect, qboolean stencil)
{
    // gapi_beginFrame skipped the frame: record no commands.
    if (!vk_frameActive())
        return;

    if(shadingDat.s_depth_attachment_dirty)
    {
        VkClearAttachment attachments;
        memset(&attachments, 0, sizeof(VkClearAttachment));

        attachments.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        attachments.clearValue.depthStencil.depth = 1.0f;

        if (stencil) {
            attachments.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            attachments.clearValue.depthStencil.stencil = 0;
        }

        VkClearRect clear_rect;
        clear_rect.rect = vk_toRect2D(rect);
        clear_rect.baseArrayLayer = 0;
        clear_rect.layerCount = 1;


        if (clear_rect.rect.extent.width == 0 || clear_rect.rect.extent.height == 0)
            return;

        qvkCmdClearAttachments(vk.command_buffer, 1, &attachments, 1, &clear_rect);
    }
}




void gapi_clearColor(const gapi_rect_t* rect, const float color[4])
{
    // gapi_beginFrame skipped the frame: record no commands.
    if (!vk_frameActive())
        return;


    // ri.Printf(PRINT_ALL, "vk_clearColorAttachments\n");

    VkClearAttachment attachments[1];
    memset(attachments, 0, sizeof(VkClearAttachment));
    
    // aspectMask is a mask selecting the color, depth and/or stencil aspects
    // of the attachment to be cleared. aspectMask can include 
    // VK_IMAGE_ASPECT_COLOR_BIT for color attachments,
    // VK_IMAGE_ASPECT_DEPTH_BIT for depth/stencil attachments with a depth
    // component, and VK_IMAGE_ASPECT_STENCIL_BIT for depth/stencil attachments
    // with a stencil component. If the subpass¡¯s depth/stencil attachment
    // is VK_ATTACHMENT_UNUSED, then the clear has no effect.

    attachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    // colorAttachment is only meaningful if VK_IMAGE_ASPECT_COLOR_BIT
    // is set in aspectMask, in which case it is an index to 
    // the pColorAttachments array in the VkSubpassDescription structure
    // of the current subpass which selects the color attachment to clear.
    attachments[0].colorAttachment = 0;
    attachments[0].clearValue.color.float32[0] = color[0];
    attachments[0].clearValue.color.float32[1] = color[1];
    attachments[0].clearValue.color.float32[2] = color[2];
    attachments[0].clearValue.color.float32[3] = color[3];

/* 
	VkClearRect clear_rect[2];
	clear_rect[0].rect = vk_toRect2D(rect);
	clear_rect[0].baseArrayLayer = 0;
	clear_rect[0].layerCount = 1;
	uint32_t rect_count = 1;
  
	// Split viewport rectangle into two non-overlapping rectangles.
	// It's a HACK to prevent Vulkan validation layer's performance warning:
	//		"vkCmdClearAttachments() issued on command buffer object XXX prior to any Draw Cmds.
	//		 It is recommended you use RenderPass LOAD_OP_CLEAR on Attachments prior to any Draw."
	// 
	// NOTE: we don't use LOAD_OP_CLEAR for color attachment when we begin renderpass
	// since at that point we don't know whether we need color buffer clear (usually we don't).
    uint32_t h = clear_rect[0].rect.extent.height / 2;
    clear_rect[0].rect.extent.height = h;
    clear_rect[1] = clear_rect[0];
    clear_rect[1].rect.offset.y = h;
    rect_count = 2;
*/

    VkClearRect clear_rect[1];
	clear_rect[0].rect = vk_toRect2D(rect);
	clear_rect[0].baseArrayLayer = 0;
	clear_rect[0].layerCount = 1;

	if (clear_rect[0].rect.extent.width == 0 || clear_rect[0].rect.extent.height == 0)
		return;

	qvkCmdClearAttachments(vk.command_buffer, 1, attachments, 1, clear_rect);

}


void gapi_pushDebugGroup(const char* name)
{
    (void)name;
}


void gapi_popDebugGroup(void)
{
}

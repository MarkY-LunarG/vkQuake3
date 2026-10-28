#include <string.h>
#include <stdlib.h>

#include "VKimpl.h"
#include "vk_instance.h"
#include "tr_globals.h"
#include "vk_texture.h"
#include "vk_instance.h"
#include "vk_geometry.h"
#include "tr_pipelines.h"
#include "vk_pipeline.h"
#include "vk_image_sampler.h"
#include "vk_pipeline_export.h"
#include "vk_frame.h"
#include "vk_shaders.h"
#include "vk_depth_attachment.h"


struct Vk_Instance vk;

//
// Vulkan API functions used by the renderer.
//
PFN_vkGetInstanceProcAddr						qvkGetInstanceProcAddr;

PFN_vkCreateInstance							qvkCreateInstance;
PFN_vkEnumerateInstanceExtensionProperties		qvkEnumerateInstanceExtensionProperties;

PFN_vkCreateDevice								qvkCreateDevice;
PFN_vkDestroyInstance							qvkDestroyInstance;
PFN_vkEnumerateDeviceExtensionProperties		qvkEnumerateDeviceExtensionProperties;
PFN_vkEnumeratePhysicalDevices					qvkEnumeratePhysicalDevices;
PFN_vkGetDeviceProcAddr							qvkGetDeviceProcAddr;
PFN_vkGetPhysicalDeviceFeatures					qvkGetPhysicalDeviceFeatures;
PFN_vkGetPhysicalDeviceFormatProperties			qvkGetPhysicalDeviceFormatProperties;
PFN_vkGetPhysicalDeviceMemoryProperties			qvkGetPhysicalDeviceMemoryProperties;
PFN_vkGetPhysicalDeviceProperties				qvkGetPhysicalDeviceProperties;
PFN_vkGetPhysicalDeviceQueueFamilyProperties	qvkGetPhysicalDeviceQueueFamilyProperties;


PFN_vkDestroySurfaceKHR							qvkDestroySurfaceKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR	qvkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR		qvkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR	qvkGetPhysicalDeviceSurfacePresentModesKHR;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR		qvkGetPhysicalDeviceSurfaceSupportKHR;


PFN_vkAllocateCommandBuffers					qvkAllocateCommandBuffers;
PFN_vkAllocateDescriptorSets					qvkAllocateDescriptorSets;
PFN_vkAllocateMemory							qvkAllocateMemory;
PFN_vkBeginCommandBuffer						qvkBeginCommandBuffer;
PFN_vkBindBufferMemory							qvkBindBufferMemory;
PFN_vkBindImageMemory							qvkBindImageMemory;
PFN_vkCmdBindDescriptorSets						qvkCmdBindDescriptorSets;
PFN_vkCmdBindIndexBuffer						qvkCmdBindIndexBuffer;
// Vulkan SC: render pass 2, VK_KHR_synchronization2, VK_KHR_copy_commands2,
// timeline semaphores, and VK_EXT_extended_dynamic_state
PFN_vkCmdPipelineBarrier2KHR	qvkCmdPipelineBarrier2KHR;
PFN_vkQueueSubmit2KHR	qvkQueueSubmit2KHR;
PFN_vkCmdCopyBufferToImage2KHR	qvkCmdCopyBufferToImage2KHR;
PFN_vkCmdCopyImageToBuffer2KHR	qvkCmdCopyImageToBuffer2KHR;
PFN_vkCreateRenderPass2	qvkCreateRenderPass2;
PFN_vkCmdBeginRenderPass2	qvkCmdBeginRenderPass2;
PFN_vkCmdEndRenderPass2	qvkCmdEndRenderPass2;
PFN_vkWaitSemaphores	qvkWaitSemaphores;
PFN_vkCmdSetCullModeEXT	qvkCmdSetCullModeEXT;
PFN_vkCmdSetFrontFaceEXT	qvkCmdSetFrontFaceEXT;
PFN_vkCmdBindPipeline						qvkCmdBindPipeline;
PFN_vkCmdBindVertexBuffers						qvkCmdBindVertexBuffers;
PFN_vkCmdBlitImage								qvkCmdBlitImage;
PFN_vkCmdClearAttachments						qvkCmdClearAttachments;
PFN_vkCmdCopyImage								qvkCmdCopyImage;
PFN_vkCmdDraw									qvkCmdDraw;
PFN_vkCmdDrawIndexed							qvkCmdDrawIndexed;
PFN_vkCmdPushConstants							qvkCmdPushConstants;
PFN_vkCmdSetDepthBias							qvkCmdSetDepthBias;
PFN_vkCmdSetScissor								qvkCmdSetScissor;
PFN_vkCmdSetViewport							qvkCmdSetViewport;
PFN_vkCreateBuffer								qvkCreateBuffer;
PFN_vkCreateCommandPool							qvkCreateCommandPool;
PFN_vkCreateDescriptorPool						qvkCreateDescriptorPool;
PFN_vkCreateDescriptorSetLayout					qvkCreateDescriptorSetLayout;
PFN_vkCreateFramebuffer							qvkCreateFramebuffer;
PFN_vkCreateGraphicsPipelines					qvkCreateGraphicsPipelines;
PFN_vkCreateImage								qvkCreateImage;
PFN_vkCreateImageView							qvkCreateImageView;
PFN_vkCreatePipelineLayout						qvkCreatePipelineLayout;
PFN_vkCreateSampler								qvkCreateSampler;
PFN_vkCreateSemaphore							qvkCreateSemaphore;
PFN_vkDestroyBuffer								qvkDestroyBuffer;
PFN_vkDestroyDescriptorSetLayout				qvkDestroyDescriptorSetLayout;
PFN_vkDestroyDevice								qvkDestroyDevice;
PFN_vkDestroyFramebuffer						qvkDestroyFramebuffer;
PFN_vkDestroyImage								qvkDestroyImage;
PFN_vkDestroyImageView							qvkDestroyImageView;
PFN_vkDestroyPipeline							qvkDestroyPipeline;
PFN_vkCreatePipelineCache	qvkCreatePipelineCache;
PFN_vkDestroyPipelineCache	qvkDestroyPipelineCache;
PFN_vkDestroyPipelineLayout						qvkDestroyPipelineLayout;
PFN_vkDestroyRenderPass							qvkDestroyRenderPass;
PFN_vkDestroySampler							qvkDestroySampler;
PFN_vkDestroySemaphore							qvkDestroySemaphore;
PFN_vkDeviceWaitIdle							qvkDeviceWaitIdle;
PFN_vkEndCommandBuffer							qvkEndCommandBuffer;
PFN_vkFreeCommandBuffers						qvkFreeCommandBuffers;
PFN_vkFreeDescriptorSets						qvkFreeDescriptorSets;
PFN_vkGetBufferMemoryRequirements				qvkGetBufferMemoryRequirements;
PFN_vkGetDeviceQueue							qvkGetDeviceQueue;
PFN_vkGetImageMemoryRequirements				qvkGetImageMemoryRequirements;
PFN_vkGetImageSubresourceLayout					qvkGetImageSubresourceLayout;
PFN_vkMapMemory									qvkMapMemory;
PFN_vkUnmapMemory                               qvkUnmapMemory;
PFN_vkQueueWaitIdle								qvkQueueWaitIdle;
PFN_vkResetDescriptorPool						qvkResetDescriptorPool;
PFN_vkUpdateDescriptorSets						qvkUpdateDescriptorSets;
PFN_vkAcquireNextImageKHR						qvkAcquireNextImageKHR;
PFN_vkCreateSwapchainKHR						qvkCreateSwapchainKHR;
PFN_vkGetSwapchainImagesKHR						qvkGetSwapchainImagesKHR;
PFN_vkQueuePresentKHR							qvkQueuePresentKHR;





static qboolean vk_createInstance(void)
{
    // There is no global state in Vulkan and all per-application state
    // is stored in a VkInstance object. Creating a VkInstance object 
    // initializes the Vulkan library and allows the application to pass
    // information about itself to the implementation.
    ri.Printf(PRINT_ALL, " Creating instance: vk.instance\n");
	
    // The version of Vulkan that is supported by an instance may be 
    // different than the version of Vulkan supported by a device or
    // physical device. Because Vulkan 1.0 implementations may fail
    // with VK_ERROR_INCOMPATIBLE_DRIVER, applications should determine
    // the version of Vulkan available before calling vkCreateInstance.
    // If the vkGetInstanceProcAddr returns NULL for vkEnumerateInstanceVersion,
    // it is a Vulkan 1.0 implementation. Otherwise, the application can
    // call vkEnumerateInstanceVersion to determine the version of Vulkan.

    VkApplicationInfo appInfo;
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = NULL;
	appInfo.pApplicationName = "OpenArena";
	appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
	appInfo.pEngineName = "VulkanArena";
	appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    // apiVersion must be the highest version of Vulkan that the
    // application is designed to use, encoded as described in the
    // API Version Numbers and Semantics section. The patch version
    // number specified in apiVersion is ignored when creating an 
    // instance object. Only the major and minor versions of the 
    // instance must match those requested in apiVersion.
	appInfo.apiVersion = VKSC_API_VERSION_1_0;


	VkInstanceCreateInfo instanceCreateInfo;
	
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    // pNext is NULL or a pointer to an extension-specific structure.
	instanceCreateInfo.pNext = NULL;
    // flags is reserved for future use.
    instanceCreateInfo.flags = 0;
    // pApplicationInfo is NULL or a pointer to an instance of
    // VkApplicationInfo. If not NULL, this information helps 
    // implementations recognize behavior inherent to classes
    // of applications.
    instanceCreateInfo.pApplicationInfo = &appInfo;

    
	// check extensions availability
    //
    // Extensions may define new Vulkan commands, structures, and enumerants.
    // For compilation purposes, the interfaces defined by registered extensions,
    // including new structures and enumerants as well as function pointer types
    // for new commands, are defined in the Khronos-supplied vulkan_core.h 
    // together with the core API. However, commands defined by extensions may
    // not be available for static linking - in which case function pointers to
    // these commands should be queried at runtime as described in Command Function
    // Pointers.
    // Extensions may be provided by layers as well as by a Vulkan implementation.
    //
	// Each platform-specific extension is an instance extension.
	// The application must enable instance extensions with vkCreateInstance
	// before using them. Enable only the extensions that the renderer uses:
	// the surface extensions that the window system needs, and the debug
	// report extension in debug builds. Some other instance extensions need
	// a Vulkan version that is higher than appInfo.apiVersion.
	uint32_t nWindowExt = 0;
	if (!vk_getInstanceExtensionsImpl(&nWindowExt, NULL))
		return qfalse;

	const char** ppInstanceExt = malloc( sizeof(char *) * (nWindowExt + 1) );
	if (!vk_getInstanceExtensionsImpl(&nWindowExt, ppInstanceExt))
	{
		free(ppInstanceExt);
		return qfalse;
	}

	uint32_t nInsExt = nWindowExt;

	uint32_t i = 0;
	ri.Printf(PRINT_ALL, "--- Enable %d instance extensions: ---\n", nInsExt);
	for (i = 0; i < nInsExt; i++)
	{
		ri.Printf(PRINT_ALL, " %s\n", ppInstanceExt[i]);
	}

	instanceCreateInfo.enabledExtensionCount = nInsExt;
	instanceCreateInfo.ppEnabledExtensionNames = ppInstanceExt;

    instanceCreateInfo.enabledLayerCount = 0;
	instanceCreateInfo.ppEnabledLayerNames = NULL;


    VkResult e = qvkCreateInstance(&instanceCreateInfo, NULL, &vk.instance);
    free(ppInstanceExt);

    if(e == VK_SUCCESS)
    {
        ri.Printf(PRINT_ALL, "--- Vulkan create instance success! ---\n\n");
        return qtrue;
    }

    vk.instance = VK_NULL_HANDLE;
    if (e == VK_ERROR_INCOMPATIBLE_DRIVER)
	{
		// The requested version of Vulkan is not supported by the driver 
		// or is otherwise incompatible for implementation-specific reasons.
        ri.Printf(PRINT_WARNING,
            "The requested version of Vulkan is not supported by the driver.\n" );
    }
    else if (e == VK_ERROR_EXTENSION_NOT_PRESENT)
    {
        ri.Printf(PRINT_WARNING, "Cannot find a specified extension library.\n");
    }
    else 
    {
        ri.Printf(PRINT_WARNING, "%s, returned by qvkCreateInstance.\n", cvtResToStr(e));
    }
    return qfalse;
}



static qboolean vk_loadGlobalFunctions(void)
{
    ri.Printf(PRINT_ALL, " Loading vulkan instance functions \n");

    vk_getInstanceProcAddrImpl();

    #define INIT_INSTANCE_FUNCTION(func)                                \
    q##func = (PFN_ ## func)qvkGetInstanceProcAddr(vk.instance, #func); \
    if (q##func == NULL) {                                              \
        ri.Error(ERR_FATAL, "Failed to find entrypoint %s", #func);     \
    }

	INIT_INSTANCE_FUNCTION(vkCreateInstance)
	INIT_INSTANCE_FUNCTION(vkEnumerateInstanceExtensionProperties)

    //
	// Get instance level functions.
	//
	if (!vk_createInstance())
		return qfalse;


	INIT_INSTANCE_FUNCTION(vkCreateDevice)
	INIT_INSTANCE_FUNCTION(vkDestroyInstance)
    INIT_INSTANCE_FUNCTION(vkDestroySurfaceKHR)
	INIT_INSTANCE_FUNCTION(vkEnumerateDeviceExtensionProperties)
	INIT_INSTANCE_FUNCTION(vkEnumeratePhysicalDevices)
	INIT_INSTANCE_FUNCTION(vkGetDeviceProcAddr)

    INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceFeatures)
    INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceFormatProperties)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceMemoryProperties)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties)

    INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfacePresentModesKHR)
	INIT_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceSupportKHR)
   


    #undef INIT_INSTANCE_FUNCTION

    ri.Printf(PRINT_ALL, " Init global functions done. \n");
    return qtrue;
}

////////////////////////////////


static qboolean vk_selectPhysicalDevice(void)
{
    // After initializing the Vulkan library through a VkInstance
    // we need to look for and select a graphics card in the system
    // that supports the features we need. In fact we can select any
    // number of graphics cards and use them simultaneously.
	uint32_t gpu_count = 0;

    // Initial call to query gpu_count, then second call for gpu info.
	qvkEnumeratePhysicalDevices(vk.instance, &gpu_count, NULL);

	if (gpu_count <= 0)
	{
		ri.Printf(PRINT_WARNING, "Vulkan: no physical device found\n");
		return qfalse;
	}

    VkPhysicalDevice *pPhyDev = (VkPhysicalDevice *) malloc (sizeof(VkPhysicalDevice) * gpu_count);
    
    // TODO: multi graphic cards selection support
    VK_CHECK(qvkEnumeratePhysicalDevices(vk.instance, &gpu_count, pPhyDev));
    // For demo app we just grab the first physical device
    vk.physical_device = pPhyDev[0];
	
    free(pPhyDev);

    ri.Printf(PRINT_ALL, " Total %d graphics card, the first one is choosed. \n", gpu_count);

    ri.Printf(PRINT_ALL, " Get physical device memory properties: vk.devMemProperties \n");
    qvkGetPhysicalDeviceMemoryProperties(vk.physical_device, &vk.devMemProperties);
    return qtrue;
}



static qboolean vk_selectSurfaceFormat(void)
{
    uint32_t nSurfmt;
    
    ri.Printf(PRINT_ALL, "\n -------- vk_selectSurfaceFormat() -------- \n");


    // Get the numbers of VkFormat's that are supported
    // "vk.surface" is the surface that will be associated with the swapchain.
    // "vk.surface" must be a valid VkSurfaceKHR handle
    VK_CHECK(qvkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, vk.surface, &nSurfmt, NULL));
    assert(nSurfmt > 0);

    VkSurfaceFormatKHR *pSurfFmts = 
        (VkSurfaceFormatKHR *) malloc ( nSurfmt * sizeof(VkSurfaceFormatKHR) );

    // To query the supported swapchain format-color space pairs for a surface
    VK_CHECK(qvkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, vk.surface, &nSurfmt, pSurfFmts));

    // If the format list includes just one entry of VK_FORMAT_UNDEFINED, the surface
    // has no preferred format. Otherwise, at least one supported format will be returned.
    if ( (nSurfmt == 1) && (pSurfFmts[0].format == VK_FORMAT_UNDEFINED) )
    {
        // special case that means we can choose any format
        vk.surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
        vk.surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        ri.Printf(PRINT_ALL, "VK_FORMAT_R8G8B8A8_UNORM\n");
        ri.Printf(PRINT_ALL, "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR\n");
    }
    else
    {
        uint32_t i;
        ri.Printf(PRINT_ALL, " Total %d surface formats supported, we choose: \n", nSurfmt);

        for( i = 0; i < nSurfmt; i++)
        {
            if( ( pSurfFmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) &&
                ( pSurfFmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) )
            {

                ri.Printf(PRINT_ALL, " format = VK_FORMAT_B8G8R8A8_UNORM \n");
                ri.Printf(PRINT_ALL, " colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR \n");
                
                vk.surface_format = pSurfFmts[i];
                break;
            }
        }

        if (i == nSurfmt)
            vk.surface_format = pSurfFmts[0];
    }

    free(pSurfFmts);


    // To query the basic capabilities of a surface, needed in order to create a swapchain
	VK_CHECK(qvkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical_device, vk.surface, &vk.surface_caps));

    // VK_IMAGE_USAGE_TRANSFER_DST_BIT is required by image clear operations.
	if ((vk.surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
	{
		ri.Printf(PRINT_WARNING, "VK_IMAGE_USAGE_TRANSFER_DST_BIT is not supported by you GPU.\n");
		return qfalse;
	}

	// VK_IMAGE_USAGE_TRANSFER_SRC_BIT is required in order to take screenshots.
	if ((vk.surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0)
	{
		ri.Printf(PRINT_WARNING, "VK_IMAGE_USAGE_TRANSFER_SRC_BIT is not supported by you GPU.\n");
		return qfalse;
	}


    // To query supported format features which are properties of the physical device
	
    VkFormatProperties props;


    // To determine the set of valid usage bits for a given format,
    // call vkGetPhysicalDeviceFormatProperties.

    // ========================= color ================
    qvkGetPhysicalDeviceFormatProperties(vk.physical_device, vk.surface_format.format, &props);
    
    // Check if the device supports blitting to linear images 
    if ( props.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT )
        ri.Printf(PRINT_ALL, "--- Linear TilingFeatures supported. ---\n");

    if ( props.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT ) 
    {
        ri.Printf(PRINT_ALL, "--- Blitting from linear tiled images supported. ---\n");
    }

    if ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT )
    {
        ri.Printf(PRINT_ALL, "--- Blitting from optimal tiled images supported. ---\n");
        vk.isBlitSupported = VK_TRUE;
    }


    //=========================== depth =====================================
    qvkGetPhysicalDeviceFormatProperties(vk.physical_device, VK_FORMAT_D24_UNORM_S8_UINT, &props);
    if ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT )
    {
        ri.Printf(PRINT_ALL, " VK_FORMAT_D24_UNORM_S8_UINT optimal Tiling feature supported.\n");
        vk.fmt_DepthStencil = VK_FORMAT_D24_UNORM_S8_UINT;
    }
    else
    {
        qvkGetPhysicalDeviceFormatProperties(vk.physical_device, VK_FORMAT_D32_SFLOAT_S8_UINT, &props);

        if ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT )
        {
            ri.Printf(PRINT_ALL, " VK_FORMAT_D32_SFLOAT_S8_UINT optimal Tiling feature supported.\n");
            vk.fmt_DepthStencil = VK_FORMAT_D32_SFLOAT_S8_UINT;
        }
        else
        {
            //formats[0] = VK_FORMAT_X8_D24_UNORM_PACK32;
		    //formats[1] = VK_FORMAT_D32_SFLOAT;
            // never get here.
	        ri.Printf(PRINT_WARNING, " Failed to find depth attachment format.\n");
	        return qfalse;
        }
    }

    ri.Printf(PRINT_ALL, " -------- --------------------------- --------\n");
    return qtrue;
}


static qboolean vk_selectQueueFamilyForPresentation(void)
{
    // Almosty every operation in Vulkan, anything from drawing textures,
    // requires commands to be submitted to a queue. There are different
    // types of queues that originate from differnet queue families and
    // each family of queues allows only a subset of commands. 
    // For example, there could be a queue family allows processing of 
    // compute commands or one that only allows memory thansfer related
    // commands. We need to check which queue families are supported by
    // the device and which one of these supports the commands that we use.


    uint32_t nSurfmt;
    qvkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device, &nSurfmt, NULL);
    
    assert(nSurfmt > 0);

    VkQueueFamilyProperties* pQueueFamilies = (VkQueueFamilyProperties *) malloc (
            nSurfmt * sizeof(VkQueueFamilyProperties) );

    // To query properties of queues available on a physical device
    qvkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device, &nSurfmt, pQueueFamilies);

    // Select queue family with presentation and graphics support
    // Iterate over each queue to learn whether it supports presenting:
    vk.queue_family_index = -1;
    
    uint32_t i;
    for (i = 0; i < nSurfmt; ++i)
    {
        // To look for a queue family that has the capability of presenting
        // to our window surface
        
        VkBool32 presentation_supported = VK_FALSE;
        VK_CHECK(qvkGetPhysicalDeviceSurfaceSupportKHR(
                    vk.physical_device, i, vk.surface, &presentation_supported));

        if (presentation_supported && 
                (pQueueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            vk.queue_family_index = i;
            
            ri.Printf(PRINT_ALL, " Queue family for presentation selected: %d\n",
                    vk.queue_family_index);

            break;
        }
    }

    free(pQueueFamilies);

    if (vk.queue_family_index == -1)
    {
        ri.Printf(PRINT_WARNING, "Vulkan: failed to find queue family\n");
        return qfalse;
    }
    return qtrue;
}


// The object reservation (VkDeviceObjectReservationCreateInfo): the most
// objects of each type that are alive at one time. Memory, command pools,
// descriptor pools, and the swapchain are never freed in Vulkan SC, so for
// them the count is the total ever made.
static void vk_getObjectReservation(VkDeviceObjectReservationCreateInfo* r)
{
    memset(r, 0, sizeof(*r));
    r->sType = VK_STRUCTURE_TYPE_DEVICE_OBJECT_RESERVATION_CREATE_INFO;
    r->semaphoreRequestCount = 2 * MAX_SWAPCHAIN_IMAGES;
    r->commandBufferRequestCount = 8;
    r->fenceRequestCount = 8;
    // The image chunks, the staging, vertex, index, and screenshot buffers,
    // and the depth image: 7 allocations, all at startup
    r->deviceMemoryRequestCount = 16;
    r->bufferRequestCount = 16;
    r->imageRequestCount = MAX_DRAWIMAGES + 16;
    r->imageViewRequestCount = MAX_DRAWIMAGES + MAX_SWAPCHAIN_IMAGES + 16;
    r->pipelineCacheRequestCount = 1;
    r->pipelineLayoutRequestCount = 4;
    r->renderPassRequestCount = 4;
    r->descriptorSetLayoutRequestCount = 4;
    r->samplerRequestCount = MAX_VK_SAMPLERS;
    r->descriptorPoolRequestCount = 2;
    r->descriptorSetRequestCount = MAX_DRAWIMAGES;
    r->framebufferRequestCount = 2 * MAX_SWAPCHAIN_IMAGES;
    r->commandPoolRequestCount = 4;
    r->surfaceRequestCount = 1;
    r->swapchainRequestCount = 1;
    r->displayModeRequestCount = 1;
    r->subpassDescriptionRequestCount = 4;
    r->attachmentDescriptionRequestCount = 8;
    r->descriptorSetLayoutBindingRequestCount = 4;
    r->descriptorSetLayoutBindingLimit = 1;
    // A 2048x2048 texture has 12 mip levels
    r->maxImageViewMipLevels = 12;
    r->maxImageViewArrayLayers = 1;

    // One pipeline pool entry for each pipeline in the cache: each is created
    // once, at startup.
    static VkPipelineCacheCreateInfo cache_info;
    static VkPipelinePoolSize pool_size;
    size_t cache_size = 0;
    uint32_t num_pipelines = 0;
    const void* cache_data = vk_pipelineCacheData(&cache_size, &num_pipelines);
    if (cache_data != NULL)
    {
        memset(&cache_info, 0, sizeof(cache_info));
        cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        cache_info.flags = VK_PIPELINE_CACHE_CREATE_READ_ONLY_BIT | VK_PIPELINE_CACHE_CREATE_USE_APPLICATION_STORAGE_BIT;
        cache_info.initialDataSize = cache_size;
        cache_info.pInitialData = cache_data;

        memset(&pool_size, 0, sizeof(pool_size));
        pool_size.sType = VK_STRUCTURE_TYPE_PIPELINE_POOL_SIZE;
        pool_size.poolEntrySize = VK_PIPELINE_POOL_ENTRY_SIZE;
        pool_size.poolEntryCount = num_pipelines;

        r->pipelineCacheCreateInfoCount = 1;
        r->pPipelineCacheCreateInfos = &cache_info;
        r->pipelinePoolSizeCount = 1;
        r->pPipelinePoolSizes = &pool_size;
        r->graphicsPipelineRequestCount = num_pipelines;
    }
}


static qboolean vk_createLogicalDevice(void)
{
    static const char* device_extensions[4] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME,
        VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME
    };
    const uint32_t numDeviceExtensions = sizeof(device_extensions) / sizeof(device_extensions[0]);

    //  Not all graphics cards are capble of presenting images directly
    //  to a screen for various reasons, for example because they are 
    //  designed for servers and don't have any display outputs. 
    //  Secondly, since image presentation is heavily tied into the 
    //  window system and the surfaces associated with windows, it is
    //  not actually part of the vulkan core. You have to enable the
    //  VK_KHR_swapchain device extension after querying for its support.
    uint32_t nDevExts = 0;

    // To query the numbers of extensions available to a given physical device
    ri.Printf( PRINT_ALL, " Check for VK_KHR_swapchain extension. \n" );

    qvkEnumerateDeviceExtensionProperties( vk.physical_device, NULL, &nDevExts, NULL);

    VkExtensionProperties* pDeviceExt = 
        (VkExtensionProperties *) malloc(sizeof(VkExtensionProperties) * nDevExts);

    qvkEnumerateDeviceExtensionProperties( vk.physical_device, NULL, &nDevExts, pDeviceExt);


    uint32_t i, j;
    for (i = 0; i < numDeviceExtensions; i++)
    {
        VkBool32 found = VK_FALSE;
        for (j = 0; j < nDevExts; j++)
        {
            if (!strcmp(device_extensions[i], pDeviceExt[j].extensionName))
            {
                found = VK_TRUE;
                break;
            }
        }
        if (!found)
        {
            ri.Printf(PRINT_WARNING, "%s is not available\n", device_extensions[i]);
            free(pDeviceExt);
            return qfalse;
        }
    }

    free(pDeviceExt);


    const float priority = 1.0;
    VkDeviceQueueCreateInfo queue_desc;
    queue_desc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_desc.pNext = NULL;
    queue_desc.flags = 0;
    queue_desc.queueFamilyIndex = vk.queue_family_index;
    queue_desc.queueCount = 1;
    queue_desc.pQueuePriorities = &priority;


    // Query fine-grained feature support for this device. If APP 
    // has specific feature requirements it should check supported
    // features based on this query.
	qvkGetPhysicalDeviceFeatures(vk.physical_device, &vk.features);

    // Vulkan SC: the most objects of each type that the renderer makes, and
    // the pipeline cache. The implementation reserves them at device creation.
    vk_loadPipelineCache();
    VkDeviceObjectReservationCreateInfo reservation;
    vk_getObjectReservation(&reservation);

    VkPhysicalDeviceVulkanSC10Features sc10_features;
    memset(&sc10_features, 0, sizeof(sc10_features));
    sc10_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_SC_1_0_FEATURES;
    sc10_features.pNext = &reservation;

    // The features of the API style that this renderer uses
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic_state_features;
    memset(&dynamic_state_features, 0, sizeof(dynamic_state_features));
    dynamic_state_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    dynamic_state_features.pNext = &sc10_features;
    dynamic_state_features.extendedDynamicState = VK_TRUE;

    VkPhysicalDeviceSynchronization2FeaturesKHR sync2_features;
    memset(&sync2_features, 0, sizeof(sync2_features));
    sync2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    sync2_features.pNext = &dynamic_state_features;
    sync2_features.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features vulkan12_features;
    memset(&vulkan12_features, 0, sizeof(vulkan12_features));
    vulkan12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12_features.pNext = &sync2_features;
    vulkan12_features.timelineSemaphore = VK_TRUE;

    VkDeviceCreateInfo device_desc;
    device_desc.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_desc.pNext = &vulkan12_features;
    device_desc.flags = 0;
    device_desc.queueCreateInfoCount = 1;
    device_desc.pQueueCreateInfos = &queue_desc;
    device_desc.enabledLayerCount = 0;
    device_desc.ppEnabledLayerNames = NULL;
    device_desc.enabledExtensionCount = numDeviceExtensions;
    device_desc.ppEnabledExtensionNames = device_extensions;
    device_desc.pEnabledFeatures = &vk.features;
    

    // After selecting a physical device to use we need to set up a
    // logical device to interface with it. The logical device 
    // creation process id similar to the instance creation process
    // and describes the features we want to use. we also need to 
    // specify which queues to create now that we've queried which
    // queue families are available. You can create multiple logical
    // devices from the same physical device if you have varying requirements.
    ri.Printf( PRINT_ALL, " Create logical device: vk.device \n" );
    VkResult e = qvkCreateDevice(vk.physical_device, &device_desc, NULL, &vk.device);
    if (e != VK_SUCCESS)
    {
        ri.Printf(PRINT_WARNING, "%s, returned by qvkCreateDevice.\n", cvtResToStr(e));
        vk.device = VK_NULL_HANDLE;
        return qfalse;
    }
    return qtrue;
}


static void vk_loadDeviceFunctions(void)
{
    ri.Printf( PRINT_ALL, " Loading device level function. \n" );

    #define INIT_DEVICE_FUNCTION(func)                              \
    q##func = (PFN_ ## func)qvkGetDeviceProcAddr(vk.device, #func); \
    if (q##func == NULL) {                                          \
        ri.Error(ERR_FATAL, "Failed to find entrypoint %s", #func); \
    }     

	INIT_DEVICE_FUNCTION(vkAllocateCommandBuffers)
	INIT_DEVICE_FUNCTION(vkAllocateDescriptorSets)
	INIT_DEVICE_FUNCTION(vkAllocateMemory)
	INIT_DEVICE_FUNCTION(vkBeginCommandBuffer)
	INIT_DEVICE_FUNCTION(vkBindBufferMemory)
	INIT_DEVICE_FUNCTION(vkBindImageMemory)
	INIT_DEVICE_FUNCTION(vkCmdBindDescriptorSets)
	INIT_DEVICE_FUNCTION(vkCmdBindIndexBuffer)
	INIT_DEVICE_FUNCTION(vkCmdPipelineBarrier2KHR)
	INIT_DEVICE_FUNCTION(vkQueueSubmit2KHR)
	INIT_DEVICE_FUNCTION(vkCmdCopyBufferToImage2KHR)
	INIT_DEVICE_FUNCTION(vkCmdCopyImageToBuffer2KHR)
	INIT_DEVICE_FUNCTION(vkCreateRenderPass2)
	INIT_DEVICE_FUNCTION(vkCmdBeginRenderPass2)
	INIT_DEVICE_FUNCTION(vkCmdEndRenderPass2)
	INIT_DEVICE_FUNCTION(vkWaitSemaphores)
	INIT_DEVICE_FUNCTION(vkCmdSetCullModeEXT)
	INIT_DEVICE_FUNCTION(vkCmdSetFrontFaceEXT)
	INIT_DEVICE_FUNCTION(vkCmdBindPipeline)
	INIT_DEVICE_FUNCTION(vkCmdBindVertexBuffers)
	INIT_DEVICE_FUNCTION(vkCmdBlitImage)
	INIT_DEVICE_FUNCTION(vkCmdClearAttachments)
	INIT_DEVICE_FUNCTION(vkCmdCopyImage)
	INIT_DEVICE_FUNCTION(vkCmdDraw)
	INIT_DEVICE_FUNCTION(vkCmdDrawIndexed)
	INIT_DEVICE_FUNCTION(vkCmdPushConstants)
	INIT_DEVICE_FUNCTION(vkCmdSetDepthBias)
	INIT_DEVICE_FUNCTION(vkCmdSetScissor)
	INIT_DEVICE_FUNCTION(vkCmdSetViewport)
	INIT_DEVICE_FUNCTION(vkCreateBuffer)
	INIT_DEVICE_FUNCTION(vkCreateCommandPool)
	INIT_DEVICE_FUNCTION(vkCreateDescriptorPool)
	INIT_DEVICE_FUNCTION(vkCreateDescriptorSetLayout)
	INIT_DEVICE_FUNCTION(vkCreateFramebuffer)
	INIT_DEVICE_FUNCTION(vkCreateGraphicsPipelines)
	INIT_DEVICE_FUNCTION(vkCreateImage)
	INIT_DEVICE_FUNCTION(vkCreateImageView)
	INIT_DEVICE_FUNCTION(vkCreatePipelineLayout)
	INIT_DEVICE_FUNCTION(vkCreateSampler)
	INIT_DEVICE_FUNCTION(vkCreateSemaphore)
	INIT_DEVICE_FUNCTION(vkDestroyBuffer)
	INIT_DEVICE_FUNCTION(vkDestroyDescriptorSetLayout)
	INIT_DEVICE_FUNCTION(vkDestroyDevice)
	INIT_DEVICE_FUNCTION(vkDestroyFramebuffer)
	INIT_DEVICE_FUNCTION(vkDestroyImage)
	INIT_DEVICE_FUNCTION(vkDestroyImageView)
	INIT_DEVICE_FUNCTION(vkDestroyPipeline)
	INIT_DEVICE_FUNCTION(vkCreatePipelineCache)
	INIT_DEVICE_FUNCTION(vkDestroyPipelineCache)
	INIT_DEVICE_FUNCTION(vkDestroyPipelineLayout)
	INIT_DEVICE_FUNCTION(vkDestroyRenderPass)
	INIT_DEVICE_FUNCTION(vkDestroySampler)
	INIT_DEVICE_FUNCTION(vkDestroySemaphore)
	INIT_DEVICE_FUNCTION(vkDeviceWaitIdle)
	INIT_DEVICE_FUNCTION(vkEndCommandBuffer)
	INIT_DEVICE_FUNCTION(vkFreeCommandBuffers)
	INIT_DEVICE_FUNCTION(vkFreeDescriptorSets)
	INIT_DEVICE_FUNCTION(vkGetBufferMemoryRequirements)
	INIT_DEVICE_FUNCTION(vkGetDeviceQueue)
	INIT_DEVICE_FUNCTION(vkGetImageMemoryRequirements)
	INIT_DEVICE_FUNCTION(vkGetImageSubresourceLayout)
	INIT_DEVICE_FUNCTION(vkMapMemory)
	INIT_DEVICE_FUNCTION(vkUnmapMemory)
	INIT_DEVICE_FUNCTION(vkQueueWaitIdle)
	INIT_DEVICE_FUNCTION(vkResetDescriptorPool)
	INIT_DEVICE_FUNCTION(vkUpdateDescriptorSets)
    
	INIT_DEVICE_FUNCTION(vkCreateSwapchainKHR)
	INIT_DEVICE_FUNCTION(vkGetSwapchainImagesKHR)
    INIT_DEVICE_FUNCTION(vkAcquireNextImageKHR)
	INIT_DEVICE_FUNCTION(vkQueuePresentKHR)

    #undef INIT_DEVICE_FUNCTION
}



// Vulkan SC has no vkFreeMemory, so the renderer allocates all device memory
// at startup. The allocations go through this counter, which reports an
// allocation after startup (vk_markStartupComplete).
static PFN_vkAllocateMemory s_allocateMemory = NULL;
static uint32_t s_numAllocations = 0;
static uint32_t s_numAllocationsAfterStartup = 0;
static qboolean s_startupComplete = qfalse;

static VKAPI_ATTR VkResult VKAPI_CALL vk_countedAllocateMemory(VkDevice device,
        const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
{
    s_numAllocations++;
    if (s_startupComplete)
    {
        s_numAllocationsAfterStartup++;
        ri.Printf(PRINT_WARNING, "Vulkan SC: device memory allocated after startup (%u bytes)\n",
            (uint32_t)pAllocateInfo->allocationSize);
    }
    return s_allocateMemory(device, pAllocateInfo, pAllocator, pMemory);
}


void vk_markStartupComplete(void)
{
    s_startupComplete = qtrue;
    ri.Printf(PRINT_ALL, " Vulkan SC: %u device memory allocations at startup\n", s_numAllocations);
}


void vk_reportMemoryAllocations(void)
{
    ri.Printf(PRINT_ALL, "Vulkan SC: %u device memory allocations, %u after startup\n",
        s_numAllocations, s_numAllocationsAfterStartup);
}


qboolean vk_getProcAddress(void)
{
    if (!vk_loadGlobalFunctions())
        return qfalse;


    // The window surface needs to be created right after the instance creation,
    // because it can actually influence the presentation mode selection.
	if (!vk_createSurfaceImpl())
        return qfalse;
   
    // select physical device
    if (!vk_selectPhysicalDevice())
        return qfalse;

    if (!vk_selectSurfaceFormat())
        return qfalse;

    if (!vk_selectQueueFamilyForPresentation())
        return qfalse;

    if (!vk_createLogicalDevice())
        return qfalse;

    // Get device level functions.
    vk_loadDeviceFunctions();
    s_allocateMemory = qvkAllocateMemory;
    qvkAllocateMemory = vk_countedAllocateMemory;
    s_numAllocations = 0;
    s_numAllocationsAfterStartup = 0;
    s_startupComplete = qfalse;

    // a call to retrieve queue handle
	qvkGetDeviceQueue(vk.device, vk.queue_family_index, 0, &vk.queue);

    // The pipeline cache, with the same create info as the reservation
    size_t cache_size = 0;
    uint32_t num_pipelines = 0;
    const void* cache_data = vk_pipelineCacheData(&cache_size, &num_pipelines);
    vk.pipeline_cache = VK_NULL_HANDLE;
    if (cache_data != NULL)
    {
        VkPipelineCacheCreateInfo cache_info;
        memset(&cache_info, 0, sizeof(cache_info));
        cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        cache_info.flags = VK_PIPELINE_CACHE_CREATE_READ_ONLY_BIT | VK_PIPELINE_CACHE_CREATE_USE_APPLICATION_STORAGE_BIT;
        cache_info.initialDataSize = cache_size;
        cache_info.pInitialData = cache_data;
        VK_CHECK(qvkCreatePipelineCache(vk.device, &cache_info, NULL, &vk.pipeline_cache));
    }
    return qtrue;
}


void vk_clearProcAddress(void)
{

    if (vk.pipeline_cache != VK_NULL_HANDLE)
    {
        qvkDestroyPipelineCache(vk.device, vk.pipeline_cache, NULL);
        vk.pipeline_cache = VK_NULL_HANDLE;
    }
    ri.Printf( PRINT_ALL, " Destroy logical device: vk.device. \n" );
    // Device queues are implicitly cleaned up when the device is destroyed
    // so we don't need to do anything in clean up
    // A failed start can leave some of the objects. Destroy only the
    // objects that exist.
	if (vk.device != VK_NULL_HANDLE)
		qvkDestroyDevice(vk.device, NULL);
    
    ri.Printf( PRINT_ALL, " Destroy surface: vk.surface. \n" );
    // make sure that the surface is destroyed before the instance
	if (vk.surface != VK_NULL_HANDLE)
		qvkDestroySurfaceKHR(vk.instance, vk.surface, NULL);


    ri.Printf( PRINT_ALL, " Destroy instance: vk.instance. \n" );
	if (vk.instance != VK_NULL_HANDLE)
		qvkDestroyInstance(vk.instance, NULL);

// ===========================================================
    ri.Printf( PRINT_ALL, " clear all proc address \n" );

	qvkCreateInstance                           = NULL;
	qvkEnumerateInstanceExtensionProperties		= NULL;

	qvkCreateDevice								= NULL;
	qvkDestroyInstance							= NULL;
	qvkEnumerateDeviceExtensionProperties		= NULL;
	qvkEnumeratePhysicalDevices					= NULL;
	qvkGetDeviceProcAddr						= NULL;
	qvkGetPhysicalDeviceFeatures				= NULL;
	qvkGetPhysicalDeviceFormatProperties		= NULL;
	qvkGetPhysicalDeviceMemoryProperties		= NULL;
	qvkGetPhysicalDeviceProperties				= NULL;
	qvkGetPhysicalDeviceQueueFamilyProperties	= NULL;

    qvkDestroySurfaceKHR						= NULL;
	qvkGetPhysicalDeviceSurfaceCapabilitiesKHR	= NULL;
	qvkGetPhysicalDeviceSurfaceFormatsKHR		= NULL;
	qvkGetPhysicalDeviceSurfacePresentModesKHR	= NULL;
	qvkGetPhysicalDeviceSurfaceSupportKHR		= NULL;

	qvkAllocateCommandBuffers					= NULL;
	qvkAllocateDescriptorSets					= NULL;
	qvkAllocateMemory							= NULL;
	qvkBeginCommandBuffer						= NULL;
	qvkBindBufferMemory							= NULL;
	qvkBindImageMemory							= NULL;
	qvkCmdBindDescriptorSets					= NULL;
	qvkCmdBindIndexBuffer						= NULL;
	qvkCmdPipelineBarrier2KHR = NULL;
	qvkQueueSubmit2KHR = NULL;
	qvkCmdCopyBufferToImage2KHR = NULL;
	qvkCmdCopyImageToBuffer2KHR = NULL;
	qvkCreateRenderPass2 = NULL;
	qvkCmdBeginRenderPass2 = NULL;
	qvkCmdEndRenderPass2 = NULL;
	qvkWaitSemaphores = NULL;
	qvkCmdSetCullModeEXT = NULL;
	qvkCmdSetFrontFaceEXT = NULL;
	qvkCmdBindPipeline							= NULL;
	qvkCmdBindVertexBuffers						= NULL;
	qvkCmdBlitImage								= NULL;
	qvkCmdClearAttachments						= NULL;
	qvkCmdCopyImage								= NULL;
	qvkCmdDraw									= NULL;
	qvkCmdDrawIndexed							= NULL;
	qvkCmdPushConstants							= NULL;
	qvkCmdSetDepthBias							= NULL;
	qvkCmdSetScissor							= NULL;
	qvkCmdSetViewport							= NULL;
	qvkCreateBuffer								= NULL;
	qvkCreateCommandPool						= NULL;
	qvkCreateDescriptorPool						= NULL;
	qvkCreateDescriptorSetLayout				= NULL;
	qvkCreateFramebuffer						= NULL;
	qvkCreateGraphicsPipelines					= NULL;
	qvkCreateImage								= NULL;
	qvkCreateImageView							= NULL;
	qvkCreatePipelineLayout						= NULL;
	qvkCreateSampler							= NULL;
	qvkCreateSemaphore							= NULL;
	qvkDestroyBuffer							= NULL;
	qvkDestroyDescriptorSetLayout				= NULL;
	qvkDestroyDevice							= NULL;
	qvkDestroyFramebuffer						= NULL;
	qvkDestroyImage								= NULL;
	qvkDestroyImageView							= NULL;
	qvkDestroyPipeline							= NULL;
	qvkCreatePipelineCache = NULL;
	qvkDestroyPipelineCache = NULL;
	qvkDestroyPipelineLayout					= NULL;
	qvkDestroyRenderPass						= NULL;
	qvkDestroySampler							= NULL;
	qvkDestroySemaphore							= NULL;
	qvkDeviceWaitIdle							= NULL;
	qvkEndCommandBuffer							= NULL;
	qvkFreeCommandBuffers						= NULL;
	qvkFreeDescriptorSets						= NULL;
	qvkGetBufferMemoryRequirements				= NULL;
	qvkGetDeviceQueue							= NULL;
	qvkGetImageMemoryRequirements				= NULL;
	qvkGetImageSubresourceLayout				= NULL;
	qvkMapMemory								= NULL;
    qvkUnmapMemory                              = NULL;
	qvkQueueWaitIdle							= NULL;
	qvkResetDescriptorPool						= NULL;
	qvkUpdateDescriptorSets						= NULL;
	qvkAcquireNextImageKHR						= NULL;
	qvkCreateSwapchainKHR						= NULL;
	qvkGetSwapchainImagesKHR					= NULL;
	qvkQueuePresentKHR							= NULL;
}


const char * cvtResToStr(VkResult result)
{
    switch(result)
    {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
            return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION:
            return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
            return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        case VK_ERROR_VALIDATION_FAILED:
            return "VK_ERROR_VALIDATION_FAILED";
        case VK_ERROR_INVALID_PIPELINE_CACHE_DATA:
            return "VK_ERROR_INVALID_PIPELINE_CACHE_DATA";
        case VK_ERROR_NO_PIPELINE_MATCH:
            return "VK_ERROR_NO_PIPELINE_MATCH";
        case VK_ERROR_NOT_PERMITTED:
            return "VK_ERROR_NOT_PERMITTED";
        default:
            break;
    }

    return "UNKNOWN_ERROR";
}

#include "ref_import.h"
#include "tr_cvar.h"
#include "VKimpl.h"
#include "vk_instance.h"
#include "glConfig.h"
#include "vk_swapchain.h"
#include "vk_frame.h"
#include "vk_depth_attachment.h"


/*

A surface has changed in such a way that it is no longer compatible with the swapchain,
and further presentation requests using the swapchain will fail. Applications must 
query the new surface properties and recreate their swapchain if they wish to continue
presenting to the surface.

VK_IMAGE_LAYOUT_PRESENT_SRC_KHR must only be used for presenting a presentable image
for display. A swapchain's image must be transitioned to this layout before calling
vkQueuePresentKHR, and must be transitioned away from this layout after calling
vkAcquireNextImageKHR.

*/


// vulkan does not have the concept of a "default framebuffer", hence it requires an
// infrastruture that will own the buffers we will render to before we visualize them
// on the screen. This infrastructure is known as the swap chain and must be created
// explicity in vulkan. The swap chain is essentially a queue of images that are 
// waiting to be presented to the screen. The general purpose of the swap chain is to
// synchronize the presentation of images with the refresh rate of the screen.

// 1) Basic surface capabilities (min/max number of images in the swap chain,
//    min/max number of images in the swap chain).
// 2) Surcface formats(pixel format, color space)
// 3) Available presentation modes


// r_swapInterval: 0 is no vertical sync, 1 is vertical sync, -1 is adaptive.
static int s_swapInterval = 0;

// The swapchain must be made again before the next frame.
static qboolean s_rebuild = qfalse;

// The surface is lost and must be made again before the next frame.
static qboolean s_surfaceLost = qfalse;


void gapi_setSwapInterval(int interval)
{
    if (interval == s_swapInterval)
        return;

    s_swapInterval = interval;
    if (vk.swapchain != VK_NULL_HANDLE)
        s_rebuild = qtrue;
}


void vk_requestSwapchainRebuild(void)
{
    s_rebuild = qtrue;
}


void vk_requestSurfaceRecreate(void)
{
    s_surfaceLost = qtrue;
}


void vk_destroySwapchain(void)
{
    // Vulkan SC has no vkDestroySwapchainKHR: the swapchain stays until the
    // device is destroyed.
    vk.swapchain = VK_NULL_HANDLE;
}


// Make the swapchain, its image views, the depth image, and the framebuffers
// again for the current surface. Textures and pipelines stay. Return qfalse
// if the surface has a size of 0, for example a minimized window. Then the
// frame is skipped, and the next frame tries again.
static qboolean vk_rebuildSwapchain(void)
{
    qvkDeviceWaitIdle(vk.device);

    VK_CHECK(qvkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical_device, vk.surface, &vk.surface_caps));
    if (vk.surface_caps.currentExtent.width == 0 || vk.surface_caps.currentExtent.height == 0)
    {
        s_rebuild = qtrue;
        return qfalse;
    }

    ri.Printf(PRINT_ALL, " Rebuild the swapchain\n");

    vk_destroyFrameBuffers();
    vk_destroyDepthAttachment();
    vk_createSwapChain(vk.device, vk.surface, vk.surface_format);
    vk_createDepthAttachment(vk.swapchain_extent.width, vk.swapchain_extent.height);
    vk_createFrameBuffers();

    s_rebuild = qfalse;
    return qtrue;
}


// Make the surface again after VK_ERROR_SURFACE_LOST_KHR. If that fails,
// restart the renderer.
static qboolean vk_recreateSurface(void)
{
    qvkDeviceWaitIdle(vk.device);

    ri.Printf(PRINT_ALL, " The surface is lost. Make it again.\n");

    vk_destroyFrameBuffers();
    vk_destroyDepthAttachment();
    vk_destroySwapchain();
    qvkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
    vk.surface = VK_NULL_HANDLE;

    VkBool32 presentation_supported = VK_FALSE;
    if (vk_createSurfaceImpl())
    {
        VK_CHECK(qvkGetPhysicalDeviceSurfaceSupportKHR(
                    vk.physical_device, vk.queue_family_index, vk.surface, &presentation_supported));
    }

    if (!presentation_supported)
    {
        ri.Printf(PRINT_WARNING, "Vulkan: cannot make the surface again, restart the renderer\n");
        s_surfaceLost = qfalse;
        ri.Cmd_ExecuteText(EXEC_APPEND, "vid_restart\n");
        return qfalse;
    }

    s_surfaceLost = qfalse;
    s_rebuild = qtrue;
    return qtrue;
}


qboolean vk_prepareSwapchain(void)
{
    if (s_surfaceLost && !vk_recreateSurface())
        return qfalse;

    if (s_rebuild && !vk_rebuildSwapchain())
        return qfalse;

    return qtrue;
}


qboolean vk_rebuildSwapchainNow(void)
{
    return vk_rebuildSwapchain();
}


// create swap chain
void vk_createSwapChain(VkDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format)
{

    // The presentation is arguably the most impottant setting for the swap chain
    // because it represents the actual conditions for showing images to the screen
    // There four possible modes available in Vulkan:
    
    // 1) VK_PRESENT_MODE_IMMEDIATE_KHR: Images submitted by your application
    //    are transferred to the screen right away, which may result in tearing.
    //
    // 2) VK_PRESENT_MODE_FIFO_KHR: The swap chain is a queue where the display
    //    takes an image from the front of the queue when the display is refreshed
    //    and the program inserts rendered images at the back of the queue. If the
    //    queue is full then the program has to wait. This is most similar to 
    //    vertical sync as found in modern games
    //
    // 3) VK_PRESENT_MODE_FIFO_RELAXED_KHR: variation of 2)
    //
    // 4) VK_PRESENT_MODE_MAILBOX_KHR: another variation of 2), the image already
    //    queued are simply replaced with the newer ones. This mode can be used
    //    to avoid tearing significantly less latency issues than standard vertical
    //    sync that uses double buffering.
    //
    // we have to look for the best mode available.
	// determine present mode and swapchain image count
    VkPresentModeKHR present_mode;

    // The number of images in the swap chain, essentially the queue length
    // The implementation specifies the minimum amount of images to functions properly
    uint32_t image_count;

    {
        ri.Printf(PRINT_ALL, "\n-------- Determine present mode --------\n");
        
        uint32_t nPM, i;
        qvkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical_device, surface, &nPM, NULL);

        VkPresentModeKHR *pPresentModes = (VkPresentModeKHR *) malloc( nPM * sizeof(VkPresentModeKHR) );

        qvkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical_device, surface, &nPM, pPresentModes);

        ri.Printf(PRINT_ALL, "Minimaal mumber ImageCount required: %d, Total %d present mode supported: \n",
                vk.surface_caps.minImageCount, nPM);

        VkBool32 mailbox_supported = VK_FALSE;
        VkBool32 immediate_supported = VK_FALSE;
        VkBool32 fifo_relaxed_supported = VK_FALSE;

        for ( i = 0; i < nPM; i++)
        {
            switch(pPresentModes[i])
            {
                case VK_PRESENT_MODE_IMMEDIATE_KHR:
                    ri.Printf(PRINT_ALL, " VK_PRESENT_MODE_IMMEDIATE_KHR \n");
                    immediate_supported = VK_TRUE;
                    break;
                case VK_PRESENT_MODE_MAILBOX_KHR:
                    ri.Printf(PRINT_ALL, " VK_PRESENT_MODE_MAILBOX_KHR \n");
                    mailbox_supported = VK_TRUE;
                    break;
                case VK_PRESENT_MODE_FIFO_KHR:
                    ri.Printf(PRINT_ALL, " VK_PRESENT_MODE_FIFO_KHR \n");
                    break;
                case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
                    ri.Printf(PRINT_ALL, " VK_PRESENT_MODE_FIFO_RELAXED_KHR \n");
                    fifo_relaxed_supported = VK_TRUE;
                    break;
                default:
                    ri.Printf(PRINT_ALL, " This device do not support presentation %d\n", pPresentModes[i]);
                    break;
            }
        }

        free(pPresentModes);


        if (s_swapInterval > 0)
        {
            // Vertical sync. VK_PRESENT_MODE_FIFO_KHR mode is guaranteed to be available.
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
            image_count = MAX(2u, vk.surface_caps.minImageCount);

            ri.Printf(PRINT_ALL, "\n r_swapInterval %d: VK_PRESENT_MODE_FIFO_KHR mode. \n", s_swapInterval);
        }
        else if (s_swapInterval < 0)
        {
            // Adaptive vertical sync: a late frame does not wait.
            present_mode = fifo_relaxed_supported ? VK_PRESENT_MODE_FIFO_RELAXED_KHR : VK_PRESENT_MODE_FIFO_KHR;
            image_count = MAX(2u, vk.surface_caps.minImageCount);

            ri.Printf(PRINT_ALL, "\n r_swapInterval %d: %s mode. \n", s_swapInterval,
                    fifo_relaxed_supported ? "VK_PRESENT_MODE_FIFO_RELAXED_KHR" : "VK_PRESENT_MODE_FIFO_KHR");
        }
        else if (mailbox_supported)
        {
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            image_count = MAX(3u, vk.surface_caps.minImageCount);
            
            ri.Printf(PRINT_ALL, "\n VK_PRESENT_MODE_MAILBOX_KHR mode, minImageCount: %d. \n", image_count);
        }
        else if(immediate_supported)
        {
            present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            image_count = MAX(2u, vk.surface_caps.minImageCount);

            ri.Printf(PRINT_ALL, "\n VK_PRESENT_MODE_IMMEDIATE_KHR mode, minImageCount: %d. \n", image_count);
        }
        else
        {
            // VK_PRESENT_MODE_FIFO_KHR mode is guaranteed to be available.
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
            image_count = MAX(2u, vk.surface_caps.minImageCount);
        }

        // The Spec Say:
        // image_count must <= VkSurfaceCapabilitiesKHR.maxImageCount
        // image_count must >= VkSurfaceCapabilitiesKHR.minImageCount

        // maxImageCount is the maximum number of images the specified device
        // supports for a swapchain created for the surface, and will be either 0,
        // or greater than or equal to minImageCount. A value of 0 means that 
        // there is no limit on the number of images, though there may be limits 
        // related to the total amount of memory used by presentable images.
        
        // Formulas such as min(N, maxImageCount) are not correct, 
        // since maxImageCount may be zero.
        if (vk.surface_caps.maxImageCount == 0)
        {
            image_count = MAX_SWAPCHAIN_IMAGES;
        }
        else
        {
            image_count = MIN(image_count+1, vk.surface_caps.maxImageCount);
        }
        
        ri.Printf(PRINT_ALL, " \n minImageCount: %d, maxImageCount: %d, setted: %d\n",
        vk.surface_caps.minImageCount, vk.surface_caps.maxImageCount, image_count);

        ri.Printf(PRINT_ALL, "\n-------- ----------------------- --------\n");
    }


    {
        ri.Printf(PRINT_ALL, "\n-------- Create vk.swapchain --------\n");

        // The swap extent is the resolution of the swap chain images and its almost 
        // always exactly equal to the resolution of the window that we're drawing to.
        VkExtent2D image_extent = vk.surface_caps.currentExtent;
        if ( (image_extent.width == 0xffffffff) && (image_extent.height == 0xffffffff))
        {
            // The surface size comes from the swap chain (Wayland), so use
            // the window size that the depth image and framebuffers also use.
            int width, height;
            R_GetWinResolution(&width, &height);

            image_extent.width = MIN( vk.surface_caps.maxImageExtent.width,
                    MAX(vk.surface_caps.minImageExtent.width, (uint32_t)width) );
            image_extent.height = MIN(vk.surface_caps.maxImageExtent.height,
                    MAX(vk.surface_caps.minImageExtent.height, (uint32_t)height) );
        }

        ri.Printf(PRINT_ALL, " Surface capabilities, image_extent.width: %d, image_extent.height: %d\n",
                image_extent.width, image_extent.height);
        vk.swapchain_extent = image_extent;


        VkSwapchainCreateInfoKHR desc;
        desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        desc.pNext = NULL;
        desc.flags = 0;
        desc.surface = surface;
        // minImageCount is the minimum number of presentable images that the application needs.
        // The implementation will either create the swapchain with at least that many images, 
        // or it will fail to create the swapchain.
        //
        // minImageCount must be less than or equal to the value returned in
        // the maxImageCount member of VkSurfaceCapabilitiesKHR the structure returned
        // byvkGetPhysicalDeviceSurfaceCapabilitiesKHR for the surface 
        // if the returned maxImageCount is not zero
        //
        // minImageCount must be greater than or equal to the value returned in
        // the minImageCount member of VkSurfaceCapabilitiesKHR the structure 
        // returned by vkGetPhysicalDeviceSurfaceCapabilitiesKHR for the surface
        desc.minImageCount = image_count;
        desc.imageFormat = surface_format.format;
        desc.imageColorSpace = surface_format.colorSpace;
        desc.imageExtent = image_extent;
        desc.imageArrayLayers = 1;

        // render images to a separate image first to perform operations like post-processing
        desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        // An image is owned by one queue family at a time and ownership
        // must be explicitly transfered before using it in an another
        // queue family. This option offers the best performance.
        desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        desc.queueFamilyIndexCount = 0;
        desc.pQueueFamilyIndices = NULL;

        // we can specify that a certain transform should be applied to
        // images in the swap chain if it is support, like a 90 degree
        // clockwise rotation  or horizontal flip, To specify that you
        // do not want any transformation, simply dprcify the current
        // transformation
        desc.preTransform = vk.surface_caps.currentTransform;

        // The compositeAlpha field specifies if the alpha channel
        // should be used for blending with other windows int the
        // windows system. 
        desc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        desc.presentMode = present_mode;

        // we don't care about the color of pixels that are obscured.
        desc.clipped = VK_TRUE;

        // With Vulkan it's possible that your swap chain becomes invalid or unoptimized
        // while your application is running, for example because the window was resized.
        // In that case the swap chain actually needs to be recreated from scratch and a
        // reference to the old one must be specified in this field.
        // A rebuild gives the old swapchain, so that the presentation engine
        // can use its resources again. Then the old swapchain is destroyed.
        VkSwapchainKHR old_swapchain = vk.swapchain;
        desc.oldSwapchain = old_swapchain;
        VK_CHECK(qvkCreateSwapchainKHR(device, &desc, NULL, &vk.swapchain));
    }
    
    //
    {
        // To obtain the number of presentable images for swapchain
        VK_CHECK(qvkGetSwapchainImagesKHR(device, vk.swapchain, &vk.swapchain_image_count, NULL));

        ri.Printf(PRINT_ALL, " Swapchain image count: %d\n", vk.swapchain_image_count);

        if( vk.swapchain_image_count > MAX_SWAPCHAIN_IMAGES )
            vk.swapchain_image_count = MAX_SWAPCHAIN_IMAGES;

        // To obtain the array of presentable images associated with a swapchain
        VK_CHECK(qvkGetSwapchainImagesKHR(device, vk.swapchain, &vk.swapchain_image_count, vk.swapchain_images_array));

        uint32_t i;
        for (i = 0; i < vk.swapchain_image_count; i++)
        {
            VkImageViewCreateInfo desc;
            desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            desc.pNext = NULL;
            desc.flags = 0;
            desc.image = vk.swapchain_images_array[i];
            desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
            desc.format = vk.surface_format.format;
            desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            desc.subresourceRange.baseMipLevel = 0;
            desc.subresourceRange.levelCount = 1;
            desc.subresourceRange.baseArrayLayer = 0;
            desc.subresourceRange.layerCount = 1;
            VK_CHECK(qvkCreateImageView(device, &desc, NULL, &vk.swapchain_image_views[i]));
        }
    }
}

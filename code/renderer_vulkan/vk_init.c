#include "VKimpl.h"
#include "vk_instance.h"
#include "vk_frame.h"
#include "vk_cmd.h"
#include "vk_depth_attachment.h"
#include "vk_pipeline.h"
#include "vk_geometry.h"
#include "vk_shaders.h"
#include "graphics_api/gapi.h"
#include "glConfig.h"

// vk_init have nothing to do with tr_init
// vk_instance should be small
// 
// After calling this function we get fully functional vulkan subsystem.
qboolean gapi_init(struct SDL_Window* window)
{
    // This function is responsible for initializing a valid Vulkan subsystem.
    vk_setWindowImpl(window);

    vk_getProcAddress(); 
 
	// Swapchain. vk.physical_device required to be init. 
	vk_createSwapChain(vk.device, vk.surface, vk.surface_format);

	// Sync primitives.
    vk_create_sync_primitives();

	// we have to create a command pool before we can create command buffers
    // command pools manage the memory that is used to store the buffers and
    // command buffers are allocated from them.
    ri.Printf(PRINT_ALL, " Create command pool: vk.command_pool \n");
    vk_create_command_pool(&vk.command_pool);
    
    ri.Printf(PRINT_ALL, " Create command buffer: vk.command_buffer \n");
    vk_create_command_buffer(vk.command_pool, &vk.command_buffer);


    int width;
    int height;

    R_GetWinResolution(&width, &height);


    // Depth attachment image.
    vk_createDepthAttachment(width, height);

    vk_createFrameBuffers(width, height);

	// Pipeline layout.
	// You can use uniform values in shaders, which are globals similar to
    // dynamic state variables that can be changes at the drawing time to
    // alter the behavior of your shaders without having to recreate them.
    // They are commonly used to create texture samplers in the fragment 
    // shader. The uniform values need to be specified during pipeline
    // creation by creating a VkPipelineLayout object.
    
    vk_createPipelineLayout();

	//
	vk_createVertexBuffer();
    vk_createIndexBuffer();;
	//
	// Shader modules.
	//
	vk_loadShaderModules();

	//
	// Standard pipelines.
	//

    vk.isInitialized = VK_TRUE;

    return qtrue;
}


qboolean gapi_isInitialized(void)
{
    return vk.isInitialized ? qtrue : qfalse;
}


void gapi_waitIdle(void)
{
    qvkDeviceWaitIdle(vk.device);
}

// Shutdown vulkan subsystem by releasing resources acquired by Vk_Instance.
void gapi_shutdown(void)
{
    ri.Printf( PRINT_ALL, "gapi_shutdown()\n" );

    vk_destroyDepthAttachment();

    vk_destroyFrameBuffers();

    vk_destroy_shading_data();

    vk_destroy_sync_primitives();
    
    vk_destroyShaderModules();

//
    vk_destroyPipelineLayout();
//
    vk_destroy_commands();

	vk_clearProcAddress();

    ri.Printf( PRINT_ALL, " clear vk struct: vk \n" );
	memset(&vk, 0, sizeof(vk));


    vk.isInitialized = VK_FALSE;
}


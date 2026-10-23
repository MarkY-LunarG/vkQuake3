/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/


// the window surface needs to be createdd right after the instance creation
// because it can actually influence the physical device selection

#include "VKimpl.h"
#include "vk_instance.h"

#include "ref_import.h"


#ifdef _WIN32
    #include "../../../SDL2/include/SDL.h"
    #include "../../../SDL2/include/SDL_vulkan.h"
#else

  #ifdef USE_LOCAL_HEADERS
    #include "../../../SDL2/include/SDL.h"
    #include "../../../SDL2/include/SDL_vulkan.h"
  #else
    #include <SDL2/SDL.h>
    #include <SDL2/SDL_syswm.h>
    #include <SDL2/SDL_vulkan.h>
  #endif

#endif


// The window that the renderer made (sdl_window.c).
static SDL_Window* window_sdl = NULL;


void vk_setWindowImpl(SDL_Window* window)
{
    window_sdl = window;
}


uint32_t gapi_sdlWindowFlags(void)
{
    return SDL_WINDOW_VULKAN;
}


void vk_getInstanceProcAddrImpl(void)
{
	ri.Printf(PRINT_ALL, " *** Vulkan Initialization ***\n");
    int code = SDL_Vulkan_LoadLibrary(NULL);
    if (code) {
        ri.Error(ERR_FATAL, "Failed to load Vulkan library (code %d): %s", code, SDL_GetError());
    }
    // Create the window 

    qvkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) SDL_Vulkan_GetVkGetInstanceProcAddr();
    if( qvkGetInstanceProcAddr == NULL)
    {
        ri.Error(ERR_FATAL, "Failed to find entrypoint vkGetInstanceProcAddr\n"); 
    }
    
    ri.Printf(PRINT_ALL,  " Get instance proc address. (using SDL2)\n");
}


void vk_getInstanceExtensionsImpl(uint32_t* pCount, const char** ppNames)
{
    unsigned int count = *pCount;

    if (!SDL_Vulkan_GetInstanceExtensions(window_sdl, &count, ppNames))
    {
        ri.Error(ERR_FATAL, "SDL_Vulkan_GetInstanceExtensions(): %s\n", SDL_GetError());
    }

    *pCount = count;
}


void vk_createSurfaceImpl(void)
{
    ri.Printf(PRINT_ALL, " Create Surface: vk.surface.\n");

    if(!SDL_Vulkan_CreateSurface(window_sdl, vk.instance, &vk.surface))
    {
        vk.surface = VK_NULL_HANDLE;
        ri.Error(ERR_FATAL, "SDL_Vulkan_CreateSurface(): %s\n", SDL_GetError());
    }
}

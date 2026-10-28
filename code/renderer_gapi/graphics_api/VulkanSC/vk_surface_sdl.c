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


// Vulkan SC presents through VK_KHR_display, not through a window surface.
// The SDL window stays for input, hidden: QVKSC_SHOW_WINDOW=1 shows it.

#include "VKimpl.h"
#include "vk_instance.h"

#include "ref_import.h"
#include "glConfig.h"


#ifdef _WIN32
    #include "../../../SDL2/include/SDL.h"
#else

  #ifdef USE_LOCAL_HEADERS
    #include "../../../SDL2/include/SDL.h"
  #else
    #include <SDL2/SDL.h>
  #endif

#endif


// The window that the renderer made (sdl_window.c).
static SDL_Window* window_sdl = NULL;
// The Vulkan SC loader, or a layer that forwards to it (QVKSC_LIBRARY).
static void* vulkansc_library = NULL;


void vk_setWindowImpl(SDL_Window* window)
{
    window_sdl = window;
}


uint32_t gapi_sdlWindowFlags(void)
{
    return 0;
}


void vk_getInstanceProcAddrImpl(void)
{
	ri.Printf(PRINT_ALL, " *** Vulkan SC Initialization ***\n");

#ifdef _WIN32
    const char* library_name = "vulkansc-1.dll";
#else
    const char* library_name = "libvulkansc.so.1";
#endif
    const char* override_name = getenv("QVKSC_LIBRARY");
    if (override_name != NULL && override_name[0] != '\0')
    {
        library_name = override_name;
    }

    if (vulkansc_library == NULL)
    {
        vulkansc_library = SDL_LoadObject(library_name);
        if (vulkansc_library == NULL)
        {
            ri.Error(ERR_FATAL, "Failed to load the Vulkan SC loader %s: %s", library_name, SDL_GetError());
        }
    }

    qvkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) SDL_LoadFunction(vulkansc_library, "vkGetInstanceProcAddr");
    if( qvkGetInstanceProcAddr == NULL)
    {
        ri.Error(ERR_FATAL, "%s has no vkGetInstanceProcAddr\n", library_name);
    }

    ri.Printf(PRINT_ALL, " Get instance proc address from %s\n", library_name);
}


qboolean vk_getInstanceExtensionsImpl(uint32_t* pCount, const char** ppNames)
{
    static const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_DISPLAY_EXTENSION_NAME
    };
    const uint32_t count = sizeof(extensions) / sizeof(extensions[0]);

    if (ppNames != NULL)
    {
        uint32_t i;
        for (i = 0; i < count && i < *pCount; i++)
        {
            ppNames[i] = extensions[i];
        }
    }
    *pCount = count;
    return qtrue;
}


qboolean vk_debugFailInitImpl(void)
{
    return qfalse;
}


#define MAX_DISPLAYS        16
#define MAX_DISPLAY_MODES   64
#define MAX_DISPLAY_PLANES  16

// The first display of the first physical device, its largest mode (then the
// highest refresh rate), and the first plane that can show that display.
qboolean vk_createSurfaceImpl(void)
{
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices =
        (PFN_vkEnumeratePhysicalDevices) qvkGetInstanceProcAddr(vk.instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceDisplayPropertiesKHR getDisplayProperties =
        (PFN_vkGetPhysicalDeviceDisplayPropertiesKHR) qvkGetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceDisplayPropertiesKHR");
    PFN_vkGetDisplayModePropertiesKHR getDisplayModeProperties =
        (PFN_vkGetDisplayModePropertiesKHR) qvkGetInstanceProcAddr(vk.instance, "vkGetDisplayModePropertiesKHR");
    PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR getPlaneProperties =
        (PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR) qvkGetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
    PFN_vkGetDisplayPlaneSupportedDisplaysKHR getPlaneSupportedDisplays =
        (PFN_vkGetDisplayPlaneSupportedDisplaysKHR) qvkGetInstanceProcAddr(vk.instance, "vkGetDisplayPlaneSupportedDisplaysKHR");
    PFN_vkCreateDisplayPlaneSurfaceKHR createDisplayPlaneSurface =
        (PFN_vkCreateDisplayPlaneSurfaceKHR) qvkGetInstanceProcAddr(vk.instance, "vkCreateDisplayPlaneSurfaceKHR");

    if (!enumeratePhysicalDevices || !getDisplayProperties || !getDisplayModeProperties ||
        !getPlaneProperties || !getPlaneSupportedDisplays || !createDisplayPlaneSurface)
    {
        ri.Printf(PRINT_WARNING, "Vulkan SC: the instance has no VK_KHR_display entry points\n");
        return qfalse;
    }

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t device_count = 1;
    VkResult result = enumeratePhysicalDevices(vk.instance, &device_count, &physical_device);
    if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || device_count == 0)
    {
        ri.Printf(PRINT_WARNING, "Vulkan SC: no physical device\n");
        return qfalse;
    }

    VkDisplayPropertiesKHR displays[MAX_DISPLAYS];
    uint32_t display_count = MAX_DISPLAYS;
    result = getDisplayProperties(physical_device, &display_count, displays);
    if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || display_count == 0)
    {
        ri.Printf(PRINT_WARNING, "Vulkan SC: no display\n");
        return qfalse;
    }
    const VkDisplayKHR display = displays[0].display;

    VkDisplayModePropertiesKHR modes[MAX_DISPLAY_MODES];
    uint32_t mode_count = MAX_DISPLAY_MODES;
    result = getDisplayModeProperties(physical_device, display, &mode_count, modes);
    if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || mode_count == 0)
    {
        ri.Printf(PRINT_WARNING, "Vulkan SC: the display has no mode\n");
        return qfalse;
    }
    const VkDisplayModePropertiesKHR* best_mode = &modes[0];
    uint32_t i;
    for (i = 1; i < mode_count; i++)
    {
        const VkDisplayModeParametersKHR* p = &modes[i].parameters;
        const VkDisplayModeParametersKHR* best = &best_mode->parameters;
        const uint64_t area = (uint64_t)p->visibleRegion.width * p->visibleRegion.height;
        const uint64_t best_area = (uint64_t)best->visibleRegion.width * best->visibleRegion.height;
        if (area > best_area || (area == best_area && p->refreshRate > best->refreshRate))
        {
            best_mode = &modes[i];
        }
    }

    VkDisplayPlanePropertiesKHR planes[MAX_DISPLAY_PLANES];
    uint32_t plane_count = MAX_DISPLAY_PLANES;
    result = getPlaneProperties(physical_device, &plane_count, planes);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        ri.Printf(PRINT_WARNING, "Vulkan SC: no display plane\n");
        return qfalse;
    }

    uint32_t plane_index;
    for (plane_index = 0; plane_index < plane_count; plane_index++)
    {
        if (planes[plane_index].currentDisplay != VK_NULL_HANDLE && planes[plane_index].currentDisplay != display)
        {
            continue;
        }

        VkDisplayKHR supported[MAX_DISPLAYS];
        uint32_t supported_count = MAX_DISPLAYS;
        result = getPlaneSupportedDisplays(physical_device, plane_index, &supported_count, supported);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE)
        {
            continue;
        }
        qboolean shows_display = qfalse;
        for (i = 0; i < supported_count; i++)
        {
            shows_display = shows_display || (supported[i] == display);
        }
        if (!shows_display)
        {
            continue;
        }

        VkDisplaySurfaceCreateInfoKHR desc;
        memset(&desc, 0, sizeof(desc));
        desc.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
        desc.displayMode = best_mode->displayMode;
        desc.planeIndex = plane_index;
        desc.planeStackIndex = planes[plane_index].currentStackIndex;
        desc.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        desc.globalAlpha = 1.0f;
        desc.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
        desc.imageExtent = best_mode->parameters.visibleRegion;

        result = createDisplayPlaneSurface(vk.instance, &desc, NULL, &vk.surface);
        if (result != VK_SUCCESS)
        {
            vk.surface = VK_NULL_HANDLE;
            ri.Printf(PRINT_WARNING, "Vulkan SC: vkCreateDisplayPlaneSurfaceKHR failed: %s\n", cvtResToStr(result));
            return qfalse;
        }

        // The game draws at the size of the display mode. The window gets the
        // same size, so that its mouse coordinates match.
        const uint32_t width = desc.imageExtent.width;
        const uint32_t height = desc.imageExtent.height;
        R_SetWinMode(-2, width, height, best_mode->parameters.refreshRate / 1000);
        SDL_SetWindowSize(window_sdl, width, height);
        const char* show_window = getenv("QVKSC_SHOW_WINDOW");
        if (show_window == NULL || show_window[0] == '\0' || show_window[0] == '0')
        {
            SDL_HideWindow(window_sdl);
        }

        ri.Printf(PRINT_ALL, " Vulkan SC display: %s, mode %ux%u at %.3f Hz, plane %u\n",
            displays[0].displayName ? displays[0].displayName : "(no name)", width, height,
            best_mode->parameters.refreshRate / 1000.0, plane_index);
        return qtrue;
    }

    ri.Printf(PRINT_WARNING, "Vulkan SC: no display plane can show the display\n");
    return qfalse;
}

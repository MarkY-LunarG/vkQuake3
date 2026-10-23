// The WebGPU surface of the SDL window. SDL gives the native window handles,
// and the surface source struct of the window system takes them.

#include "wgpu_local.h"

#ifdef _WIN32
    #include "../../../SDL2/include/SDL.h"
    #include "../../../SDL2/include/SDL_syswm.h"
#else
  #ifdef USE_LOCAL_HEADERS
    #include "../../../SDL2/include/SDL.h"
    #include "../../../SDL2/include/SDL_syswm.h"
    #include "../../../SDL2/include/SDL_metal.h"
  #else
    #include <SDL2/SDL.h>
    #include <SDL2/SDL_syswm.h>
    #include <SDL2/SDL_metal.h>
  #endif
#endif


uint32_t gapi_sdlWindowFlags(void)
{
#ifdef __APPLE__
    return SDL_WINDOW_METAL;
#else
    return 0;
#endif
}


WGPUSurface wgpu_createSurfaceImpl(WGPUInstance instance, SDL_Window* window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);

    if (!SDL_GetWindowWMInfo(window, &info))
    {
        ri.Printf(PRINT_WARNING, "SDL_GetWindowWMInfo(): %s\n", SDL_GetError());
        return NULL;
    }

    WGPUSurfaceDescriptor desc = WGPU_SURFACE_DESCRIPTOR_INIT;

    switch (info.subsystem)
    {
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
        case SDL_SYSWM_WAYLAND:
        {
            WGPUSurfaceSourceWaylandSurface source = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
            source.display = info.info.wl.display;
            source.surface = info.info.wl.surface;
            desc.nextInChain = &source.chain;
            ri.Printf(PRINT_ALL, " Create a WebGPU surface for a Wayland window.\n");
            return wgpuInstanceCreateSurface(instance, &desc);
        }
#endif
#if defined(SDL_VIDEO_DRIVER_X11)
        case SDL_SYSWM_X11:
        {
            WGPUSurfaceSourceXlibWindow source = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
            source.display = info.info.x11.display;
            source.window = (uint64_t)info.info.x11.window;
            desc.nextInChain = &source.chain;
            ri.Printf(PRINT_ALL, " Create a WebGPU surface for an X11 window.\n");
            return wgpuInstanceCreateSurface(instance, &desc);
        }
#endif
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
        case SDL_SYSWM_WINDOWS:
        {
            WGPUSurfaceSourceWindowsHWND source = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
            source.hwnd = info.info.win.window;
            source.hinstance = info.info.win.hinstance;
            desc.nextInChain = &source.chain;
            ri.Printf(PRINT_ALL, " Create a WebGPU surface for a Windows window.\n");
            return wgpuInstanceCreateSurface(instance, &desc);
        }
#endif
#if defined(SDL_VIDEO_DRIVER_COCOA)
        case SDL_SYSWM_COCOA:
        {
            // The Metal view stays until the window is destroyed.
            SDL_MetalView view = SDL_Metal_CreateView(window);
            WGPUSurfaceSourceMetalLayer source = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
            source.layer = SDL_Metal_GetLayer(view);
            desc.nextInChain = &source.chain;
            ri.Printf(PRINT_ALL, " Create a WebGPU surface for a Metal layer.\n");
            return wgpuInstanceCreateSurface(instance, &desc);
        }
#endif
        default:
            ri.Printf(PRINT_WARNING, "WebGPU: the SDL window system %d is not supported\n", info.subsystem);
            return NULL;
    }
}

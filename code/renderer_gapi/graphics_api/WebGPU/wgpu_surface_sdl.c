// The WebGPU surface of the SDL window. SDL gives the native window handles,
// and the surface source struct of the window system takes them.

#include "wgpu_local.h"

#ifdef _WIN32
    // The local SDL_config.h is the macOS configuration, and MinGW builds use
    // it too. SDL_syswm.h declares the Windows window handles only when
    // SDL_VIDEO_DRIVER_WINDOWS is defined.
    #include "../../../SDL2/include/SDL.h"
    #undef SDL_VIDEO_DRIVER_COCOA
    #ifndef SDL_VIDEO_DRIVER_WINDOWS
        #define SDL_VIDEO_DRIVER_WINDOWS 1
    #endif
    #include "../../../SDL2/include/SDL_syswm.h"
#else
  #ifdef USE_LOCAL_HEADERS
    #include "../../../SDL2/include/SDL.h"
    #ifndef __APPLE__
        // The local SDL_config.h is the macOS configuration. SDL_syswm.h
        // declares the Wayland and X11 window handles only when
        // SDL_VIDEO_DRIVER_WAYLAND and SDL_VIDEO_DRIVER_X11 are defined.
        #undef SDL_VIDEO_DRIVER_COCOA
        #ifndef SDL_VIDEO_DRIVER_WAYLAND
            #define SDL_VIDEO_DRIVER_WAYLAND 1
        #endif
        #ifndef SDL_VIDEO_DRIVER_X11
            #define SDL_VIDEO_DRIVER_X11 1
        #endif
    #endif
    #include "../../../SDL2/include/SDL_syswm.h"
    #include "../../../SDL2/include/SDL_metal.h"
  #else
    #include <SDL2/SDL.h>
    #include <SDL2/SDL_syswm.h>
    #include <SDL2/SDL_metal.h>
  #endif
#endif

#ifdef __APPLE__
    #include <objc/message.h>
    #include <objc/runtime.h>
#endif

uint32_t gapi_sdlWindowFlags(void)
{
#if defined(__APPLE__) && SDL_VERSION_ATLEAST(2, 0, 14)
    return SDL_WINDOW_METAL;
#elif defined(__APPLE__)
    // SDL 2.0.12 (code/libs/macosx) has no SDL_WINDOW_METAL flag.
    // SDL_Metal_CreateView works without it.
    return 0;
#elif !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    // On Wayland and X11, Dawn presents with a Vulkan swapchain, as the Vulkan
    // backend does. The flag tells SDL that a Vulkan swapchain attaches the
    // buffers of the window.
    return SDL_WINDOW_VULKAN;
#else
    return 0;
#endif
}


WGPUSurface wgpu_createSurfaceImpl(WGPUInstance instance, SDL_Window* window)
{
#ifdef __EMSCRIPTEN__
    // The SDL window of Emscripten is the canvas with the id "canvas"
    // (Module.canvas in the web page).
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvas.selector = wgpu_stringView("#canvas");
    WGPUSurfaceDescriptor canvasDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
    canvasDesc.nextInChain = &canvas.chain;
    ri.Printf(PRINT_ALL, " Create a WebGPU surface for the canvas.\n");
    return wgpuInstanceCreateSurface(instance, &canvasDesc);
#else
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
#if defined(_WIN32)
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
#if defined(__APPLE__)
        case SDL_SYSWM_COCOA:
        {
            // The Metal view stays until the window is destroyed.
            SDL_MetalView view = SDL_Metal_CreateView(window);
            if (view == NULL)
            {
                ri.Printf(PRINT_WARNING, "SDL_Metal_CreateView(): %s\n", SDL_GetError());
                return NULL;
            }
            // The view is an NSView with a CAMetalLayer. SDL 2.0.12 has no
            // SDL_Metal_GetLayer, so get the layer with the Objective-C runtime.
            typedef void* (*GetLayerFn)(void*, SEL);
            WGPUSurfaceSourceMetalLayer source = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
            source.layer = ((GetLayerFn)objc_msgSend)(view, sel_registerName("layer"));
            desc.nextInChain = &source.chain;
            ri.Printf(PRINT_ALL, " Create a WebGPU surface for a Metal layer.\n");
            return wgpuInstanceCreateSurface(instance, &desc);
        }
#endif
        default:
            ri.Printf(PRINT_WARNING, "WebGPU: the SDL window system %d is not supported\n", info.subsystem);
            return NULL;
    }
#endif
}

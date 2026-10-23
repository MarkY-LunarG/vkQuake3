# WebGPU backend

This directory has the WebGPU backend of `renderer_gapi`.
It uses Dawn, through the C interface in `webgpu/webgpu.h`.
The shared renderer code calls only the functions in `graphics_api/gapi.h`.

## Build

The Makefile does not build the WebGPU renderer by default.
First build Dawn with `scripts/build_dawn.sh`.
The script installs Dawn into `build/dawn-install`.
Then build the game with `BUILD_RENDERER_WEBGPU=1`.
The build copies the Dawn library next to the renderer.

Start the game with `+set cl_renderer webgpu`.

### Linux

Dawn uses Vulkan, with Wayland and X11 surfaces.
The Wayland and X11 development packages are necessary.

```sh
scripts/build_dawn.sh
make BUILD_RENDERER_WEBGPU=1
```

### Windows

Note: this configuration is not tested.

Dawn uses D3D12.
The script builds Dawn with MSVC.
The MinGW build of the game can use the Dawn DLL, because Dawn has a C interface.
Visual Studio 2022 with the C++ workload, the Windows SDK, Git, CMake, and Python are necessary.

1. Open MSYS2 (MINGW64) or Git Bash.
   For a Ninja build, open it from a Visual Studio developer prompt, so that `cl.exe` is in `PATH`.
   If `cl.exe` is not in `PATH`, the script uses the Visual Studio generator.
2. Build Dawn:

   ```sh
   scripts/build_dawn.sh
   ```

3. Build the game in MSYS2 (MINGW64) with MinGW:

   ```sh
   make BUILD_RENDERER_WEBGPU=1
   ```

The build copies `webgpu_dawn.dll` (and `dxcompiler.dll` and `dxil.dll`, if Dawn installs them)
next to `ioquake3.x86_64.exe`.
Windows finds the DLL in the directory of the executable.

### macOS

Note: this configuration is not tested.

Dawn uses Metal.
Xcode or the Command Line Tools, CMake, and Ninja are necessary.
The Makefile and the SDL library in `code/libs/macosx` support only x86_64.
On an Apple silicon Mac, the x86_64 game and Dawn run with Rosetta.

```sh
scripts/build_dawn.sh -a x86_64
make ARCH=x86_64 BUILD_RENDERER_WEBGPU=1
```

The renderer finds `libwebgpu_dawn.dylib` in its own directory (`@loader_path`).
`make-macosx-app.sh` does not put the Vulkan or the WebGPU renderer into the application bundle.
Run the game from the build directory.

## Backend

| File | Content |
|---|---|
| `wgpu_init.c` | Instance, surface, adapter, device, callbacks, shutdown, and the backend information |
| `wgpu_surface_sdl.c` | The surface of the SDL window: Wayland, X11, Windows, or a Metal layer |
| `wgpu_frame.c` | Surface configuration, the depth and stencil texture, and the one render pass of each frame |
| `wgpu_texture.c` | Textures, samplers, and one bind group for each texture |
| `wgpu_pipeline.c` | Shader modules, pipeline layouts, and render pipelines from the GLS state |
| `wgpu_geometry.c` | Vertex and index buffers, the vertex shader constants, and the draws |
| `wgpu_draw.c` | Clears, and the copy of the frame for screenshots and video capture |

The WGSL shaders in `shaders/` come from the GLSL shaders in `graphics_api/shaders/glsl`.
`compile.sh` in that directory makes them with `glslangValidator`, `spirv-opt`, and `tint`.

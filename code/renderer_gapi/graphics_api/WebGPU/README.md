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
See [Run](#run).

### Linux

Dawn uses Vulkan, with Wayland and X11 surfaces.
The Wayland and X11 development packages are necessary.

```sh
scripts/build_dawn.sh
make BUILD_RENDERER_WEBGPU=1
```

This build uses the libraries and the SDL headers in `code/`.
If the compiler cannot find `opus/opusfile.h`, add `USE_CODEC_OPUS=0`.

To use the libraries of the system, add `USE_INTERNAL_LIBS=0`.
On Fedora, install these packages first:

```sh
sudo dnf install sdl2-compat-devel libogg-devel libvorbis-devel zlib-ng-compat-devel \
    libjpeg-turbo-devel openal-soft-devel opusfile-devel
make BUILD_RENDERER_WEBGPU=1 USE_INTERNAL_LIBS=0
```

The Makefile does not record these settings in the object files.
When you change `USE_INTERNAL_LIBS`, use `make clean` or another `BUILD_DIR`.
If you do not, the build can link objects of the two settings.
For example, the game then shows `Wrong JPEG library version` and the textures do not load.

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

### Browser

The browser build has only the client with the WebGPU renderer.
It uses the Emscripten SDK, and the `emdawnwebgpu` port of Emscripten gives `webgpu.h`.
The game code runs as QVM from `pak8.pk3`.

```sh
source <emsdk>/emsdk_env.sh
emmake make PLATFORM=emscripten
```

The WebGPU code waits for the device at startup and for the pixels of a screenshot.
`EMSCRIPTEN_ASYNC` selects how the browser build waits:

| Value | `.wasm` size | Browsers |
|---|---|---|
| `jspi` (default) | 2.0 MB | Browsers with JavaScript Promise Integration (tested in Chrome 156 and Firefox 158) |
| `asyncify` | 4.9 MB | All browsers with WebGPU (tested in Chrome 156 and Firefox 158) |

For a browser without JSPI, use `emmake make PLATFORM=emscripten EMSCRIPTEN_ASYNC=asyncify`.
Use another `BUILD_DIR` for each value, because the object files are not the same.

The output in `build/release-emscripten-wasm32` is `ioquake3_webgpu.wasm32.js`,
`ioquake3_webgpu.wasm32.wasm`, `ioquake3_webgpu.html`, and `ioquake3_webgpu-config.json`.
The game data is not part of the build.
The page loads the files in `ioquake3_webgpu-config.json` from its data URL, which must have
`baseq3/` with your `pak0.pk3` to `pak8.pk3`.

1. Serve the build directory and the game data from a web server.
   For example, make a link to the game directory and start a local server:

   ```sh
   cd build/release-emscripten-wasm32
   ln -s "<Quake 3 directory>" gamedata
   python3 -m http.server 8000 --bind 127.0.0.1
   ```

2. Open `http://127.0.0.1:8000/ioquake3_webgpu.html?data=gamedata/` in a browser with WebGPU.
   Firefox 158 on Linux has WebGPU only when `dom.webgpu.enabled` is `true` in `about:config`.
   Chrome 156 on Linux gives a WebGPU adapter only with Vulkan, for example when it starts with
   `--enable-features=Vulkan`.
3. Click the canvas to capture the mouse.
   Escape releases the mouse and opens the game menu.
   Add game arguments with `args`, and write `+` as `%2b`, for example
   `&args=%2bdevmap%20q3dm17`.

## Run

The game data is not part of the build.
`fs_basepath` is the directory that has `baseq3/` with your `pak0.pk3` to `pak8.pk3`.

```sh
build/release-linux-x86_64/ioquake3.x86_64 +set fs_basepath "<Quake 3 directory>" \
    +set cl_renderer webgpu
```

Useful settings:

| Setting | Effect |
|---|---|
| `+set fs_homepath <directory>` | Keep the configuration and the screenshots in this directory, not in `~/.q3a` |
| `+set r_fullscreen 0 +set r_mode 3` | Use a 640 x 480 window |
| `+set timedemo 1 +demo four` | Play the demo as fast as possible, then print the frame rate |
| `+set r_apiCoverage heavy` | Use more of the WebGPU API (see [API coverage](#api-coverage)) |
| `SDL_VIDEODRIVER=x11` (environment) | Use X11 (XWayland) and not Wayland |

On Linux, the renderer tries Wayland first, then X11.
`r_sdlDriver` selects one driver.

The `quit` command stops the game.
SIGTERM and SIGINT (Ctrl+C) also stop it: the game quits at the start of the next frame.
A second SIGTERM or SIGINT stops the process immediately.

The game writes `ioq3.pid` into the game directory of `fs_homepath`, and removes it at a normal
exit.
If the file is there at startup, an "Abnormal Exit" dialog asks if the game must start with
safe video settings.
The game waits until you answer.

## Test cvars

| Cvar | Effect |
|---|---|
| `r_webgpuFatalErrors 1` | The first uncaptured WebGPU error stops the game with a fatal error |
| `r_webgpuOffscreen 1` | The frame is drawn into an offscreen texture on all surfaces (set it at startup) |

Screenshots and video capture copy the texture of the frame.
If the surface has no `CopySrc` usage, the backend always draws into an offscreen texture and
copies it to the surface at the end of the frame.
`r_webgpuOffscreen 1` tests this path on a surface that has `CopySrc`.

## API coverage

`r_apiCoverage` selects how much of the WebGPU API the backend uses.
It is for captures that test the API coverage of a capture and replay tool.
Set it at startup, for example `+set r_apiCoverage heavy`, or set it and use `vid_restart`.

| Value | Effect |
|---|---|
| `default` | The usual path. The backend uses only the calls that it needs to draw |
| `heavy` | The backend also uses the calls in the list below. The frames do not change |

With `heavy`, the backend does these steps:

- It puts each view (world, portal, mirror, or model) into a render pass debug group.
  It puts the copies at the end of the frame into a command encoder debug group.
- It makes the render pipelines with `wgpuDeviceCreateRenderPipelineAsync`.
  The first draw with a pipeline waits for it with `wgpuInstanceWaitAny`.
- `gapiInfo` and `vkinfo` also print the adapter information of the device, the device limits,
  the device and instance features, the instance limits, and the WGSL language features.
- `gapiInfo` and `gpuMem` also print the texture, attachment, and buffer memory.
  The texture and buffer getters (`wgpuTextureGet*`, `wgpuBufferGetSize`, `wgpuBufferGetUsage`)
  give the numbers.

### Check the heavy path

The two paths must draw the same frames.
Take a screenshot at the same frame of a demo with each value, and compare the files:

```sh
for mode in default heavy; do
    build/release-linux-x86_64/ioquake3.x86_64 +set fs_basepath "<Quake 3 directory>" \
        +set cl_renderer webgpu +set r_apiCoverage $mode +set r_webgpuFatalErrors 1 \
        +set timedemo 1 +demo four +wait 600 +screenshot cov_$mode +wait 10 +quit
done
md5sum ~/.q3a/baseq3/screenshots/cov_*.tga
```

The screenshot runs in the frame after the command, thus `+wait 10` comes before `+quit`.

### More API coverage

The calls in this table are not in a capture of `demo four` with `heavy`.
Each item has a use in a real renderer.
Put a new item behind `wg.heavyCoverage`, keep the frames the same, and add it to the list above.

| Item | Calls | Size | Work |
|---|---|---|---|
| Debug markers | `wgpuRenderPassEncoderInsertDebugMarker`, `wgpuCommandEncoderInsertDebugMarker` | small | Put the shader name of each draw (`gapi_draw`) or of each clear into a marker |
| Error scopes | `wgpuDevicePushErrorScope`, `wgpuDevicePopErrorScope` | small | Put a scope around the creation of a shader module and of a pipeline, and print the name of the object that failed |
| Shader compilation messages | `wgpuShaderModuleGetCompilationInfo` | small | Print the WGSL warnings after `wgpu_createPipelineResources` makes the modules |
| Adapter information | `wgpuAdapterGetLimits`, `wgpuAdapterGetFeatures` | small | Print the adapter limits and features in `gapiInfo`, next to the device values |
| Work-done notification | `wgpuQueueOnSubmittedWorkDone` | small | Wait for the submitted work before the map of the screenshot buffer (`wgpu_draw.c`) |
| Device-lost future | `wgpuDeviceGetLostFuture` | small | Check the future with `wgpuInstanceWaitAny` and a timeout of 0 at the start of each frame |
| Mapped-range copies | `wgpuBufferGetMapState`, `wgpuBufferReadMappedRange` | small | Copy the screenshot pixels with `wgpuBufferReadMappedRange`, not with `wgpuBufferGetConstMappedRange` |
| Buffer destruction | `wgpuBufferDestroy` | small | Destroy the geometry buffers at shutdown, before the release |
| Texture labels | `wgpuTextureSetLabel` | small | Give the image name to a texture when `gapi_uploadTexture` puts a new image into it |
| GPU frame time | `wgpuDeviceCreateQuerySet` (timestamp), `wgpuCommandEncoderWriteTimestamp`, `wgpuCommandEncoderResolveQuerySet` | medium | Needs the `TimestampQuery` feature. Read the result some frames later and print it with `r_speeds` |
| Light flares | `wgpuDeviceCreateQuerySet` (occlusion), `wgpuRenderPassEncoderBeginOcclusionQuery`, `wgpuRenderPassEncoderEndOcclusionQuery`, `wgpuCommandEncoderResolveQuerySet`, `wgpuQuerySetDestroy` | large | `RB_RenderFlares` has no caller, and `tr_flares.c` reads the depth with `glReadPixels`. Replace the depth read with an occlusion query for each flare, and use the result of an earlier frame |
| GPU mipmaps | `wgpuDeviceCreateComputePipeline`, `wgpuDeviceCreateComputePipelineAsync`, the `wgpuComputePassEncoder*` calls | medium | Make the mipmap levels with a compute pass, not on the CPU |

Render bundles and indirect draws are not in the table.
Q3 makes its geometry on the CPU for each frame, thus these calls have no real use in Q3.

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

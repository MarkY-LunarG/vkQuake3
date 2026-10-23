// Start and stop the WebGPU backend: instance, surface, adapter, device, and
// queue. The order and the shutdown rules come from the Warzone 2100 WebGPU
// work: make sure that the surface works with GetCapabilities, do not keep
// the surface in the adapter options, and release the surface before the
// device.

#include "wgpu_local.h"

wgpuState_t wg;


WGPUStringView wgpu_stringView(const char* text)
{
    WGPUStringView view = { text, WGPU_STRLEN };
    return view;
}


const char* wgpu_cString(WGPUStringView view, char* buffer, size_t size)
{
    size_t length = 0;

    if (view.data != NULL)
        length = (view.length == WGPU_STRLEN) ? strlen(view.data) : view.length;
    if (length >= size)
        length = size - 1;
    if (length > 0)
        memcpy(buffer, view.data, length);
    buffer[length] = '\0';
    return buffer;
}


static void wgpu_onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
        WGPUStringView message, void* userdata1, void* userdata2)
{
    if (status == WGPURequestAdapterStatus_Success)
    {
        *(WGPUAdapter*)userdata1 = adapter;
    }
    else
    {
        char text[512];
        ri.Printf(PRINT_WARNING, "WebGPU: no adapter: %s\n", wgpu_cString(message, text, sizeof(text)));
    }
}


static void wgpu_onDevice(WGPURequestDeviceStatus status, WGPUDevice device,
        WGPUStringView message, void* userdata1, void* userdata2)
{
    if (status == WGPURequestDeviceStatus_Success)
    {
        *(WGPUDevice*)userdata1 = device;
    }
    else
    {
        char text[512];
        ri.Printf(PRINT_WARNING, "WebGPU: no device: %s\n", wgpu_cString(message, text, sizeof(text)));
    }
}


static void wgpu_onDeviceLost(WGPUDevice const* device, WGPUDeviceLostReason reason,
        WGPUStringView message, void* userdata1, void* userdata2)
{
    // gapi_shutdown destroys the device. That is not a loss.
    if (reason == WGPUDeviceLostReason_Destroyed || reason == WGPUDeviceLostReason_CallbackCancelled)
        return;

    char text[512];
    ri.Printf(PRINT_WARNING, "WebGPU error: the device is lost (%d): %s\n",
            (int)reason, wgpu_cString(message, text, sizeof(text)));
    wg.deviceLost = qtrue;
}


static void wgpu_onUncapturedError(WGPUDevice const* device, WGPUErrorType type,
        WGPUStringView message, void* userdata1, void* userdata2)
{
    char text[2048];
    ri.Printf(PRINT_WARNING, "WebGPU error (%d): %s\n", (int)type, wgpu_cString(message, text, sizeof(text)));
}


static void wgpu_onLog(WGPULoggingType type, WGPUStringView message, void* userdata1, void* userdata2)
{
    char text[2048];
    ri.Printf(type >= WGPULoggingType_Warning ? PRINT_WARNING : PRINT_DEVELOPER,
            "Dawn: %s\n", wgpu_cString(message, text, sizeof(text)));
}


// The only place that waits for the adapter and the device. In a browser
// build, this function must be different, because the browser cannot wait.
static qboolean wgpu_requestAdapterAndDevice(void)
{
    WGPURequestAdapterOptions adapterOptions = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapterOptions.powerPreference = WGPUPowerPreference_HighPerformance;
    adapterOptions.compatibleSurface = wg.surface;

    WGPURequestAdapterCallbackInfo adapterCallback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapterCallback.mode = WGPUCallbackMode_WaitAnyOnly;
    adapterCallback.callback = wgpu_onAdapter;
    adapterCallback.userdata1 = &wg.adapter;

    WGPUFutureWaitInfo wait = WGPU_FUTURE_WAIT_INFO_INIT;
    wait.future = wgpuInstanceRequestAdapter(wg.instance, &adapterOptions, adapterCallback);
    if (wgpuInstanceWaitAny(wg.instance, 1, &wait, UINT64_MAX) != WGPUWaitStatus_Success || wg.adapter == NULL)
        return qfalse;

    // The adapter options keep a reference to the surface only for the request.
    adapterOptions.compatibleSurface = NULL;

    // Request only the optional features that the adapter has.
    WGPUFeatureName features[1];
    size_t featureCount = 0;
    if (wgpuAdapterHasFeature(wg.adapter, WGPUFeatureName_ClipDistances))
        features[featureCount++] = WGPUFeatureName_ClipDistances;

    WGPUDeviceDescriptor deviceDesc = WGPU_DEVICE_DESCRIPTOR_INIT;
    deviceDesc.label = wgpu_stringView("Quake 3");
    deviceDesc.requiredFeatureCount = featureCount;
    deviceDesc.requiredFeatures = features;
    deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceDesc.deviceLostCallbackInfo.callback = wgpu_onDeviceLost;
    deviceDesc.uncapturedErrorCallbackInfo.callback = wgpu_onUncapturedError;

    WGPURequestDeviceCallbackInfo deviceCallback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    deviceCallback.mode = WGPUCallbackMode_WaitAnyOnly;
    deviceCallback.callback = wgpu_onDevice;
    deviceCallback.userdata1 = &wg.device;

    wait.future = wgpuAdapterRequestDevice(wg.adapter, &deviceDesc, deviceCallback);
    wait.completed = 0;
    if (wgpuInstanceWaitAny(wg.instance, 1, &wait, UINT64_MAX) != WGPUWaitStatus_Success || wg.device == NULL)
        return qfalse;

    return qtrue;
}


static void wgpu_releaseAll(void)
{
    wgpu_releaseFrameResources();

    // Release the surface before the device. Dawn crashes in its Vulkan
    // deleter if the surface is released after the device.
    if (wg.surface)
    {
        if (wg.configured)
            wgpuSurfaceUnconfigure(wg.surface);
        wgpuSurfaceRelease(wg.surface);
    }
    if (wg.queue)
        wgpuQueueRelease(wg.queue);
    if (wg.device)
    {
        wgpuDeviceDestroy(wg.device);
        wgpuDeviceRelease(wg.device);
    }
    if (wg.adapter)
        wgpuAdapterRelease(wg.adapter);
    if (wg.instance)
    {
        // Run the callbacks of the device destruction.
        wgpuInstanceProcessEvents(wg.instance);
        wgpuInstanceRelease(wg.instance);
    }

    memset(&wg, 0, sizeof(wg));
}


qboolean gapi_init(struct SDL_Window* window)
{
    ri.Printf(PRINT_ALL, " *** WebGPU Initialization ***\n");

    memset(&wg, 0, sizeof(wg));

    WGPUInstanceFeatureName instanceFeatures[1] = { WGPUInstanceFeatureName_TimedWaitAny };
    WGPUInstanceDescriptor instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = instanceFeatures;
    wg.instance = wgpuCreateInstance(&instanceDesc);
    if (wg.instance == NULL)
    {
        ri.Printf(PRINT_WARNING, "WebGPU: wgpuCreateInstance failed\n");
        return qfalse;
    }

    wg.surface = wgpu_createSurfaceImpl(wg.instance, window);
    if (wg.surface == NULL || !wgpu_requestAdapterAndDevice())
    {
        ri.Printf(PRINT_WARNING, "WebGPU cannot start on this window.\n");
        wgpu_releaseAll();
        return qfalse;
    }

    // A bad surface descriptor gives an error object, not NULL. The surface
    // works only if it has capabilities for the adapter.
    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuSurfaceGetCapabilities(wg.surface, wg.adapter, &caps) != WGPUStatus_Success || caps.formatCount == 0)
    {
        ri.Printf(PRINT_WARNING, "WebGPU: the surface has no capabilities for the adapter.\n");
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        wgpu_releaseAll();
        return qfalse;
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    WGPULoggingCallbackInfo logging = WGPU_LOGGING_CALLBACK_INFO_INIT;
    logging.callback = wgpu_onLog;
    wgpuDeviceSetLoggingCallback(wg.device, logging);

    wg.queue = wgpuDeviceGetQueue(wg.device);

    if (!wgpu_configureSurface())
    {
        wgpu_releaseAll();
        return qfalse;
    }

    wg.initialized = qtrue;
    return qtrue;
}


void gapi_shutdown(void)
{
    ri.Printf(PRINT_ALL, "gapi_shutdown()\n");
    wgpu_releaseAll();
}


qboolean gapi_isInitialized(void)
{
    return wg.initialized;
}


void gapi_waitIdle(void)
{
    // WebGPU keeps the objects that submitted work uses until the work is
    // done, so a release does not need a wait.
    if (wg.initialized)
        wgpuInstanceProcessEvents(wg.instance);
}


void gapi_getCaps(gapi_caps_t* caps)
{
    memset(caps, 0, sizeof(*caps));

    // WebGPU has no line polygon mode.
    caps->fillModeLine = qfalse;

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(wg.adapter, &info) == WGPUStatus_Success)
    {
        wgpu_cString(info.vendor, caps->vendor, sizeof(caps->vendor));
        wgpu_cString(info.device, caps->renderer, sizeof(caps->renderer));
        wgpu_cString(info.description, caps->version, sizeof(caps->version));
        wgpuAdapterInfoFreeMembers(info);
    }
    Q_strcat(caps->version, sizeof(caps->version), " (WebGPU, Dawn)");
}


static const char* wgpu_backendName(WGPUBackendType type)
{
    switch (type)
    {
        case WGPUBackendType_D3D11: return "D3D11";
        case WGPUBackendType_D3D12: return "D3D12";
        case WGPUBackendType_Metal: return "Metal";
        case WGPUBackendType_Vulkan: return "Vulkan";
        case WGPUBackendType_OpenGL: return "OpenGL";
        case WGPUBackendType_OpenGLES: return "OpenGL ES";
        default: return "unknown";
    }
}


void gapi_printInfo(void)
{
    ri.Printf(PRINT_ALL, "\nActive 3D API: WebGPU (Dawn)\n");

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(wg.adapter, &info) == WGPUStatus_Success)
    {
        char vendor[256], device[256], description[256], architecture[256];
        ri.Printf(PRINT_ALL, "WebGPU backend: %s\n", wgpu_backendName(info.backendType));
        ri.Printf(PRINT_ALL, "WebGPU vendor: %s (0x%X)\n", wgpu_cString(info.vendor, vendor, sizeof(vendor)), info.vendorID);
        ri.Printf(PRINT_ALL, "WebGPU architecture: %s\n", wgpu_cString(info.architecture, architecture, sizeof(architecture)));
        ri.Printf(PRINT_ALL, "WebGPU device: %s (0x%X)\n", wgpu_cString(info.device, device, sizeof(device)), info.deviceID);
        ri.Printf(PRINT_ALL, "WebGPU driver: %s\n", wgpu_cString(info.description, description, sizeof(description)));
        wgpuAdapterInfoFreeMembers(info);
    }

    ri.Printf(PRINT_ALL, "WebGPU clip distances: %s\n",
            wgpuDeviceHasFeature(wg.device, WGPUFeatureName_ClipDistances) ? "yes" : "no");

    gapi_printMemoryInfo();
}

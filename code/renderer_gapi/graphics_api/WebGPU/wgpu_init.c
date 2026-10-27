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
    // Dawn calls this function from inside a WebGPU call, so ri.Error cannot
    // be called here. gapi_beginFrame examines the count.
    wg.errorCount++;
}


static void wgpu_onPopErrorScope(WGPUPopErrorScopeStatus status, WGPUErrorType type,
        WGPUStringView message, void* userdata1, void* userdata2)
{
    if (status != WGPUPopErrorScopeStatus_Success || type == WGPUErrorType_NoError)
        return;

    char text[2048];
    ri.Printf(PRINT_WARNING, "WebGPU error (%d) in %s: %s\n", (int)type, (const char*)userdata1,
            wgpu_cString(message, text, sizeof(text)));
    // The scope captured the error, so the uncaptured error callback does not count it.
    wg.errorCount++;
}


void wgpu_pushErrorScope(void)
{
    if (wg.heavyCoverage)
        wgpuDevicePushErrorScope(wg.device, WGPUErrorFilter_Validation);
}


void wgpu_popErrorScope(const char* label)
{
    if (!wg.heavyCoverage)
        return;

    // No wait: in a browser, a wait in a frame destroys the surface texture of
    // the frame. gapi_beginFrame runs the callback before it examines the
    // error count.
    WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.callback = wgpu_onPopErrorScope;
    callback.userdata1 = (void*)label;
    wgpuDevicePopErrorScope(wg.device, callback);
}


// The Dawn log. The browser WebGPU API (emdawnwebgpu) has no logging callback.
#ifndef __EMSCRIPTEN__
static void wgpu_onLog(WGPULoggingType type, WGPUStringView message, void* userdata1, void* userdata2)
{
    char text[2048];
    ri.Printf(type >= WGPULoggingType_Warning ? PRINT_WARNING : PRINT_DEVELOPER,
            "Dawn: %s\n", wgpu_cString(message, text, sizeof(text)));
}
#endif


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
    WGPUFeatureName features[2];
    size_t featureCount = 0;
    if (wgpuAdapterHasFeature(wg.adapter, WGPUFeatureName_ClipDistances))
    {
        features[featureCount++] = WGPUFeatureName_ClipDistances;
        wg.clipDistances = qtrue;
    }
    // r_apiCoverage heavy: timestamp queries measure the render pass on the GPU.
    if (wg.heavyCoverage && wgpuAdapterHasFeature(wg.adapter, WGPUFeatureName_TimestampQuery))
    {
        features[featureCount++] = WGPUFeatureName_TimestampQuery;
        wg.timestampQuery = qtrue;
    }

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

    // r_apiCoverage heavy: gapi_beginFrame examines this future, in addition to the callback.
    if (wg.heavyCoverage)
        wg.lostFuture = wgpuDeviceGetLostFuture(wg.device);

    return qtrue;
}


static void wgpu_releaseAll(void)
{
    wgpu_releaseFrameResources();
    if (wg.device)
    {
        wgpu_releaseTimingResources();
        wgpu_releaseOcclusionResources();
        wgpu_releaseDrawResources();
        wgpu_releasePipelineResources();
        wgpu_releaseGeometryResources();
        wgpu_releaseTextureResources();
    }

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

    // Tests set this to stop at the first WebGPU error.
    wg.fatalErrors = ri.Cvar_Get("r_webgpuFatalErrors", "0", CVAR_TEMP);
    // Tests set this to draw into an offscreen texture on all surfaces.
    wg.forceOffscreen = ri.Cvar_Get("r_webgpuOffscreen", "0", CVAR_LATCH | CVAR_TEMP);
    // "heavy" uses more of the WebGPU API than the default path, for captures that
    // test the API coverage of a capture tool (graphics_api/WebGPU/README.md).
    wg.apiCoverage = ri.Cvar_Get("r_apiCoverage", "default", CVAR_LATCH | CVAR_TEMP);
    wg.heavyCoverage = (Q_stricmp(wg.apiCoverage->string, "heavy") == 0) ? qtrue : qfalse;
    if (wg.heavyCoverage)
        ri.Printf(PRINT_ALL, " WebGPU: r_apiCoverage heavy\n");
    else if (Q_stricmp(wg.apiCoverage->string, "default") != 0)
        ri.Printf(PRINT_WARNING, "WebGPU: r_apiCoverage \"%s\" is not \"default\" or \"heavy\"; using \"default\"\n",
                wg.apiCoverage->string);

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

#ifndef __EMSCRIPTEN__
    WGPULoggingCallbackInfo logging = WGPU_LOGGING_CALLBACK_INFO_INIT;
    logging.callback = wgpu_onLog;
    wgpuDeviceSetLoggingCallback(wg.device, logging);
#endif

    wg.queue = wgpuDeviceGetQueue(wg.device);

    if (!wgpu_configureSurface())
    {
        wgpu_releaseAll();
        return qfalse;
    }

    // The pipelines need the surface format, so they come after the
    // surface configuration.
    wgpu_createTextureResources();
    wgpu_createGeometryResources();
    wgpu_createPipelineResources();
    wgpu_createDrawResources();
    wgpu_createTimingResources();
    wgpu_createOcclusionResources();

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
    // r_apiCoverage heavy: occlusion queries for the light flares (r_flares).
    caps->occlusionQueries = wg.heavyCoverage;
    // r_apiCoverage heavy: the GPU makes the mipmaps of r_simpleMipMaps 1.
    caps->mipmapGeneration = wgpu_mipmapGeneration();

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


// r_apiCoverage heavy: print the features and the limits of the device and the
// instance. The numbers are WGPUFeatureName, WGPUInstanceFeatureName, and
// WGPUWGSLLanguageFeatureName values.
static void wgpu_printApiCoverageInfo(void)
{
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuDeviceGetAdapterInfo(wg.device, &info) == WGPUStatus_Success)
    {
        char device[256];
        ri.Printf(PRINT_ALL, "WebGPU device adapter: %s\n", wgpu_cString(info.device, device, sizeof(device)));
        wgpuAdapterInfoFreeMembers(info);
    }

    WGPULimits limits = WGPU_LIMITS_INIT;
    if (wgpuDeviceGetLimits(wg.device, &limits) == WGPUStatus_Success)
        ri.Printf(PRINT_ALL, "WebGPU device limits: maxTextureDimension2D %u, maxBindGroups %u\n",
                limits.maxTextureDimension2D, limits.maxBindGroups);

    // The adapter can give more than the device asked for.
    WGPULimits adapterLimits = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(wg.adapter, &adapterLimits) == WGPUStatus_Success)
        ri.Printf(PRINT_ALL, "WebGPU adapter limits: maxTextureDimension2D %u, maxBindGroups %u\n",
                adapterLimits.maxTextureDimension2D, adapterLimits.maxBindGroups);

    WGPUSupportedFeatures adapterFeatures = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(wg.adapter, &adapterFeatures);
    ri.Printf(PRINT_ALL, "WebGPU adapter features (%u):", (unsigned)adapterFeatures.featureCount);
    for (size_t i = 0; i < adapterFeatures.featureCount; i++)
        ri.Printf(PRINT_ALL, " 0x%X", (unsigned)adapterFeatures.features[i]);
    ri.Printf(PRINT_ALL, "\n");
    wgpuSupportedFeaturesFreeMembers(adapterFeatures);

    WGPUSupportedFeatures features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuDeviceGetFeatures(wg.device, &features);
    ri.Printf(PRINT_ALL, "WebGPU device features (%u):", (unsigned)features.featureCount);
    for (size_t i = 0; i < features.featureCount; i++)
        ri.Printf(PRINT_ALL, " 0x%X", (unsigned)features.features[i]);
    ri.Printf(PRINT_ALL, "\n");
    wgpuSupportedFeaturesFreeMembers(features);

    WGPUSupportedInstanceFeatures instanceFeatures = WGPU_SUPPORTED_INSTANCE_FEATURES_INIT;
    wgpuGetInstanceFeatures(&instanceFeatures);
    ri.Printf(PRINT_ALL, "WebGPU instance features (%u):", (unsigned)instanceFeatures.featureCount);
    for (size_t i = 0; i < instanceFeatures.featureCount; i++)
        ri.Printf(PRINT_ALL, " 0x%X", (unsigned)instanceFeatures.features[i]);
    ri.Printf(PRINT_ALL, "\n");
    wgpuSupportedInstanceFeaturesFreeMembers(instanceFeatures);

    WGPUInstanceLimits instanceLimits = WGPU_INSTANCE_LIMITS_INIT;
    if (wgpuGetInstanceLimits(&instanceLimits) == WGPUStatus_Success)
        ri.Printf(PRINT_ALL, "WebGPU instance limits: timedWaitAnyMaxCount %u\n",
                (unsigned)instanceLimits.timedWaitAnyMaxCount);
    ri.Printf(PRINT_ALL, "WebGPU timed WaitAny: %s\n",
            wgpuHasInstanceFeature(WGPUInstanceFeatureName_TimedWaitAny) ? "yes" : "no");

    WGPUSupportedWGSLLanguageFeatures wgsl = WGPU_SUPPORTED_WGSL_LANGUAGE_FEATURES_INIT;
    wgpuInstanceGetWGSLLanguageFeatures(wg.instance, &wgsl);
    ri.Printf(PRINT_ALL, "WebGPU WGSL language features (%u):", (unsigned)wgsl.featureCount);
    for (size_t i = 0; i < wgsl.featureCount; i++)
        ri.Printf(PRINT_ALL, " 0x%X", (unsigned)wgsl.features[i]);
    ri.Printf(PRINT_ALL, "\n");
    wgpuSupportedWGSLLanguageFeaturesFreeMembers(wgsl);
    ri.Printf(PRINT_ALL, "WebGPU WGSL packed 4x8 integer dot product: %s\n",
            wgpuInstanceHasWGSLLanguageFeature(wg.instance, WGPUWGSLLanguageFeatureName_Packed4x8IntegerDotProduct)
                    ? "yes" : "no");
    wgpu_printTimingInfo();
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

    if (wg.heavyCoverage)
        wgpu_printApiCoverageInfo();

    gapi_printMemoryInfo();
}

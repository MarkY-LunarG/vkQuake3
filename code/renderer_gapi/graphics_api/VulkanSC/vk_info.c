#include "tr_local.h"
#include "ref_import.h"
#include "VKimpl.h"
#include "vk_instance.h"

// Information about the Vulkan instance and device (gapi.h).


// ==================================================
// ==================================================

static void printDeviceExtensions(void)
{
    uint32_t nDevExts = 0;

    // To query the extensions available to a given physical device
    VK_CHECK( qvkEnumerateDeviceExtensionProperties( vk.physical_device, NULL, &nDevExts, NULL) );

    assert(nDevExts > 0);

    VkExtensionProperties* pDevExt = 
        (VkExtensionProperties *) malloc(sizeof(VkExtensionProperties) * nDevExts);

    qvkEnumerateDeviceExtensionProperties(
            vk.physical_device, NULL, &nDevExts, pDevExt);


    ri.Printf(PRINT_ALL, "--------- Total %d Device Extension Supported ---------\n", nDevExts);
    uint32_t i;
    for (i=0; i<nDevExts; ++i)
    {
        ri.Printf(PRINT_ALL, " %s \n", pDevExt[i].extensionName);
    }
    ri.Printf(PRINT_ALL, "--------- ----------------------------------- ---------\n");

    free(pDevExt);
}


static void printInstanceExtensions(void)
{
    uint32_t i = 0;

	uint32_t nInsExt = 0;
    // To retrieve a list of supported extensions before creating an instance
	VK_CHECK( qvkEnumerateInstanceExtensionProperties( NULL, &nInsExt, NULL) );

    assert(nInsExt > 0);

    VkExtensionProperties* pInsExt = (VkExtensionProperties *) malloc(sizeof(VkExtensionProperties) * nInsExt);
    
    VK_CHECK(qvkEnumerateInstanceExtensionProperties( NULL, &nInsExt, pInsExt));

    ri.Printf(PRINT_ALL, "\n");

    ri.Printf(PRINT_ALL, "----- Total %d Instance Extension Supported -----\n", nInsExt);
    for (i = 0; i < nInsExt; ++i)
    {            
        ri.Printf(PRINT_ALL, "%s\n", pInsExt[i].extensionName );
    }
    ri.Printf(PRINT_ALL, "----- ------------------------------------- -----\n\n");
   
    free(pInsExt);
    // ==================================================================
/*    
    uint32_t nDevExts = 0;

    // To query the extensions available to a given physical device
    VK_CHECK( qvkEnumerateDeviceExtensionProperties( vk.physical_device, NULL, &nDevExts, NULL) );

    assert(nDevExts > 0);

    VkExtensionProperties* pDevExt = 
        (VkExtensionProperties *) malloc(sizeof(VkExtensionProperties) * nDevExts);

    qvkEnumerateDeviceExtensionProperties(
            vk.physical_device, NULL, &nDevExts, pDevExt);


    ri.Printf(PRINT_ALL, "---- Total %d Device Extension Supported ----\n", nDevExts);
    uint32_t i;
    for (i=0; i<nDevExts; ++i)
    {
        ri.Printf(PRINT_ALL, " %s \n", pDevExt[i].extensionName);
    }
    ri.Printf(PRINT_ALL, "---- ----------------------------------- ----\n\n");

// There much more device extentions, beyound UI driver info can display
// and we only use VK_KHR_swapchain for now, so won't copy that,
// just print it out.
    
    for (i = 0; i < nDevExts; ++i)
    {    
        uint32_t len = strlen(pDevExt[i].extensionName);
        memcpy(glConfig.extensions_string + indicator, pDevExt[i].extensionName, len);
        indicator += len;
        glConfig.extensions_string[indicator++] = ' ';
    }
    free(pDevExt);
*/

}



static const char* vk_vendorName(uint32_t vendorID)
{
    if (vendorID == 0x1002) {
        return "Advanced Micro Devices, Inc.";
    } else if (vendorID == 0x10DE) {
        return "NVIDIA";
    } else if (vendorID == 0x8086) {
        return "Intel Corporation";
    }
    return "unknown";
}


void gapi_getCaps(gapi_caps_t* caps)
{
    memset(caps, 0, sizeof(*caps));

    caps->fillModeLine = vk.features.fillModeNonSolid ? qtrue : qfalse;

    // The instance extensions that are available, for the UI.
    uint32_t i, nInsExt = 0;
    VK_CHECK( qvkEnumerateInstanceExtensionProperties( NULL, &nInsExt, NULL) );
    VkExtensionProperties* pInsExt = (VkExtensionProperties *) malloc(sizeof(VkExtensionProperties) * nInsExt);
    VK_CHECK( qvkEnumerateInstanceExtensionProperties( NULL, &nInsExt, pInsExt) );
    for (i = 0; i < nInsExt; ++i)
    {
        if (strlen(caps->extensions) + strlen(pInsExt[i].extensionName) + 2 > sizeof(caps->extensions))
            break;
        Q_strcat(caps->extensions, sizeof(caps->extensions), pInsExt[i].extensionName);
        Q_strcat(caps->extensions, sizeof(caps->extensions), " ");
    }
    free(pInsExt);

    VkPhysicalDeviceProperties props;
    qvkGetPhysicalDeviceProperties(vk.physical_device, &props);

    snprintf(caps->version, sizeof(caps->version), " Vk api version: %d.%d.%d ",
            VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
            VK_API_VERSION_PATCH(props.apiVersion));
    Q_strncpyz(caps->vendor, vk_vendorName(props.vendorID), sizeof(caps->vendor));
    Q_strncpyz(caps->renderer, props.deviceName, sizeof(caps->renderer));
    if (*caps->renderer && caps->renderer[strlen(caps->renderer) - 1] == '\n')
        caps->renderer[strlen(caps->renderer) - 1] = 0;
}



void gapi_printInfo( void )
{
    ri.Printf( PRINT_ALL, "\nActive 3D API: Vulkan\n" );

    // To query general properties of physical devices once enumerated
    VkPhysicalDeviceProperties props;
    qvkGetPhysicalDeviceProperties(vk.physical_device, &props);

    uint32_t major = VK_API_VERSION_MAJOR(props.apiVersion);
    uint32_t minor = VK_API_VERSION_MINOR(props.apiVersion);
    uint32_t patch = VK_API_VERSION_PATCH(props.apiVersion);

    const char* device_type;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        device_type = "INTEGRATED_GPU";
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        device_type = "DISCRETE_GPU";
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)
        device_type = "VIRTUAL_GPU";
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
        device_type = "CPU";
    else
        device_type = "Unknown";

    const char* vendor_name = vk_vendorName(props.vendorID);

    ri.Printf(PRINT_ALL, "Vk api version: %d.%d.%d\n", major, minor, patch);
    ri.Printf(PRINT_ALL, "Vk driver version: %d\n", props.driverVersion);
    ri.Printf(PRINT_ALL, "Vk vendor id: 0x%X (%s)\n", props.vendorID, vendor_name);
    ri.Printf(PRINT_ALL, "Vk device id: 0x%X\n", props.deviceID);
    ri.Printf(PRINT_ALL, "Vk device type: %s\n", device_type);
    ri.Printf(PRINT_ALL, "Vk device name: %s\n", props.deviceName);

    //
	// Info that for UI display
	//
    printInstanceExtensions();

    printDeviceExtensions();

    gapi_printMemoryInfo();
}

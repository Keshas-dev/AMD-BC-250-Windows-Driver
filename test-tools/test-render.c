#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g_log = NULL;

void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a);
    if (g_log) { va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }
}

typedef struct {
    int Major;
    int Minor;
    int Patch;
    char Name[256];
} VERSION_INFO;

typedef struct {
    float Position[3];
    float Color[3];
} Vertex;

int main() {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\render-test.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== BC-250 RENDER TEST ===\n\n");

    // 1. Test Vulkan ICD exists
    Log("1. Checking Vulkan ICD...\n");
    HMODULE hVulkan = LoadLibraryA("vulkan-1.dll");
    if (!hVulkan) {
        Log("   Vulkan not found!\n");
        fclose(g_log);
        return 1;
    }
    Log("   Vulkan loaded: OK\n");

    // Get Vulkan functions
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = 
        (PFN_vkGetInstanceProcAddr)GetProcAddress(hVulkan, "vkGetInstanceProcAddr");
    
    PFN_vkCreateInstance vkCreateInstance = 
        (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    
    if (!vkCreateInstance) {
        Log("   Cannot get vkCreateInstance!\n");
        FreeLibrary(hVulkan);
        fclose(g_log);
        return 1;
    }
    Log("   vkCreateInstance: OK\n");

    // 2. Create Vulkan instance
    Log("\n2. Creating Vulkan instance...\n");
    
    VkApplicationInfo appInfo = {0};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "BC-250 Render Test";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instInfo = {0};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    VkInstance instance;
    VkResult result = vkCreateInstance(&instInfo, NULL, &instance);
    if (result != VK_SUCCESS) {
        Log("   CreateInstance FAILED: %d\n", result);
    } else {
        Log("   CreateInstance: OK\n");
    }

    // 3. Get physical devices
    Log("\n3. Enumerating physical devices...\n");
    
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices =
        (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties =
        (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties =
        (PFN_vkGetPhysicalDeviceMemoryProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties");
    PFN_vkGetInstanceProcAddr vkGetDeviceProcAddr =
        (PFN_vkGetInstanceProcAddr)vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");

    if (!vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceProperties) {
        Log("   Cannot get device functions!\n");
        FreeLibrary(hVulkan);
        fclose(g_log);
        return 1;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    
    if (deviceCount == 0) {
        Log("   NO PHYSICAL DEVICES! GPU not accessible!\n");
    } else {
        Log("   Found %u physical devices\n", deviceCount);
        
        VkPhysicalDevice physicalDevice = NULL;
        vkEnumeratePhysicalDevices(instance, &deviceCount, &physicalDevice);
        
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        
        Log("   Device: %s\n", props.deviceName);
        Log("   Vendor: 0x%04X\n", props.vendorID);
        Log("   Device: 0x%04X\n", props.deviceID);
        Log("   API: %d.%d.%d\n", 
            VK_VERSION_MAJOR(props.apiVersion),
            VK_VERSION_MINOR(props.apiVersion), 
            VK_VERSION_PATCH(props.apiVersion));
        Log("   Driver: %d.%d.%d\n",
            VK_VERSION_MAJOR(props.driverVersion),
            VK_VERSION_MINOR(props.driverVersion),
            VK_VERSION_PATCH(props.driverVersion));
        
        // Get memory info
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        Log("\n   Memory Heaps: %u\n", memProps.memoryHeapCount);
        for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
            Log("   Heap %u: %llu MB (%s)\n", i,
                memProps.memoryHeaps[i].size / (1024*1024),
                (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "VRAM" : "System");
        }
        
        // 4. Try to create logical device
        Log("\n4. Creating logical device...\n");
        
        PFN_vkCreateDevice vkCreateDevice = 
            (PFN_vkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
        PFN_vkDestroyDevice vkDestroyDevice = 
            (PFN_vkDestroyDevice)vkGetInstanceProcAddr(instance, "vkDestroyDevice");
        PFN_vkDestroyInstance vkDestroyInstance = 
            (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");
        
        if (vkCreateDevice && vkDestroyDevice) {
            VkDevice device;
            VkDeviceCreateInfo devInfo = {0};
            devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            
            result = vkCreateDevice(physicalDevice, &devInfo, NULL, &device);
            if (result == VK_SUCCESS) {
                Log("   CreateDevice: OK\n");
                Log("\n*** GPU IS WORKING - CAN CREATE DEVICE! ***\n");
                vkDestroyDevice(device, NULL);
            } else {
                Log("   CreateDevice FAILED: %d\n", result);
                Log("\n*** GPU CREATION FAILED ***\n");
            }
        }
    }

    vkDestroyInstance(instance, NULL);
    FreeLibrary(hVulkan);

    Log("\n=== RENDER TEST COMPLETE ===\n");
    fclose(g_log);

    printf("Done. Check output\\render-test.log\n");
    return 0;
}
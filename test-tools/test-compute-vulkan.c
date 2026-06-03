#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <vulkan/vulkan.h>

static FILE *g_log = NULL;
void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a);
    if (g_log) { va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }
}

int main() {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\compute-vulkan.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== BC-250 COMPUTE SHADER TEST (Vulkan SDK) ===\n\n");

    // Load Vulkan loader (system vulkan-1.dll will find our ICD)
    HMODULE hVulkan = LoadLibraryA("vulkan-1.dll");
    if (!hVulkan) { Log("Vulkan loader not found!\n"); return 1; }

    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = 
        (PFN_vkGetInstanceProcAddr)GetProcAddress(hVulkan, "vkGetInstanceProcAddr");

    // Load all functions
    #define VK_LOAD(name) name = (PFN_##name)vkGetInstanceProcAddr(NULL, #name)
    
    PFN_vkCreateInstance vkCreateInstance; VK_LOAD(vkCreateInstance);
    PFN_vkDestroyInstance vkDestroyInstance; VK_LOAD(vkDestroyInstance);
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices; VK_LOAD(vkEnumeratePhysicalDevices);
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties; VK_LOAD(vkGetPhysicalDeviceProperties);
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties; VK_LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
    PFN_vkCreateDevice vkCreateDevice; VK_LOAD(vkCreateDevice);
    PFN_vkDestroyDevice vkDestroyDevice; VK_LOAD(vkDestroyDevice);
    PFN_vkGetDeviceQueue vkGetDeviceQueue; VK_LOAD(vkGetDeviceQueue);
    
    Log("Functions loaded: OK\n");

    // Create instance
    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "BC-250 Compute", 0, "", 0, VK_API_VERSION_1_0 };
    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &appInfo, 0, NULL, 0, NULL };
    
    VkInstance instance;
    VkResult res = vkCreateInstance(&instInfo, NULL, &instance);
    Log("CreateInstance: %s (res=%d)\n", res == VK_SUCCESS ? "OK" : "FAILED", res);
    if (res != VK_SUCCESS) { FreeLibrary(hVulkan); fclose(g_log); return 1; }

    // Enumerate devices
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(instance, &devCount, NULL);
    Log("Physical devices: %u\n", devCount);

    if (devCount == 0) { Log("No devices!\n"); vkDestroyInstance(instance, NULL); FreeLibrary(hVulkan); fclose(g_log); return 1; }

    VkPhysicalDevice physDev;
    vkEnumeratePhysicalDevices(instance, &devCount, &physDev);
    
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physDev, &props);
    Log("Device: %s (Vendor=0x%04X Device=0x%04X)\n", props.deviceName, props.vendorID, props.deviceID);

    // Get queue families
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physDev, &qCount, NULL);
    
    VkQueueFamilyProperties* queues = (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physDev, &qCount, queues);
    
    int computeQ = -1;
    for (uint32_t i = 0; i < qCount; i++) {
        Log("Queue %u: flags=0x%08X count=%u\n", i, queues[i].queueFlags, queues[i].queueCount);
        if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) computeQ = i;
    }
    
    if (computeQ < 0) { Log("No compute queue!\n"); free(queues); vkDestroyInstance(instance, NULL); FreeLibrary(hVulkan); fclose(g_log); return 1; }
    
    Log("Using compute queue: %u\n", computeQ);
    
    // Create device
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, (uint32_t)computeQ, 1, &priority };
    
    VkDeviceCreateInfo devInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qInfo, 0, NULL, 0, NULL, NULL };
    
    VkDevice device;
    res = vkCreateDevice(physDev, &devInfo, NULL, &device);
    Log("CreateDevice: %s (res=%d)\n", res == VK_SUCCESS ? "OK" : "FAILED", res);
    
    if (res == VK_SUCCESS) {
        VkQueue queue;
        vkGetDeviceQueue(device, computeQ, 0, &queue);
        Log("Got compute queue: %s\n", queue != NULL ? "OK" : "FAILED");
        
        Log("\n========================================\n");
        Log("  *** GPU CAN DO COMPUTE! ***\n");
        Log("========================================\n");
        
        vkDestroyDevice(device, NULL);
    }
    
    free(queues);
    vkDestroyInstance(instance, NULL);
    FreeLibrary(hVulkan);
    
    Log("\n=== COMPUTE TEST COMPLETE ===\n");
    fclose(g_log);
    printf("Done. Check output\\compute-vulkan.log\n");
    return 0;
}
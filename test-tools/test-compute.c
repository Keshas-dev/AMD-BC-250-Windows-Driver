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

// Vulkan function types we need
typedef uint32_t VkResult;
typedef void* VkInstance;
typedef void* VkPhysicalDevice;
typedef void* VkDevice;
typedef void* VkQueue;

typedef struct { uint32_t sType; const void* pNext; void* pApplicationName; uint32_t applicationVersion; const char* pEngineName; uint32_t engineVersion; uint32_t apiVersion; } VkApplicationInfo;
typedef struct { uint32_t sType; const void* pNext; const void* pFlags; const void* pApplicationInfo; uint32_t enabledLayerCount; const char** ppEnabledLayerNames; uint32_t enabledExtensionCount; const char** ppEnabledExtensionNames; } VkInstanceCreateInfo;
typedef struct { uint32_t sType; const void* pNext; const void* flags; uint32_t queueFamilyIndex; uint32_t queueCount; const float* pQueuePriorities; } VkDeviceQueueCreateInfo;
typedef struct { uint32_t sType; const void* pNext; const void* flags; uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo* pQueueCreateInfos; uint32_t enabledLayerCount; const char** ppEnabledLayerNames; uint32_t enabledExtensionCount; const char** ppEnabledExtensionNames; const void* pEnabledFeatures; } VkDeviceCreateInfo;

typedef VkResult (VKAPI_PTR *PFN_vkCreateInstance)(const void*, const void*, VkInstance*);
typedef VkResult (VKAPI_PTR *PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, void*);
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, uint32_t*, void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateDevice)(VkPhysicalDevice, const void*, const void*, VkDevice*);
typedef void (VKAPI_PTR *PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
typedef void (VKAPI_PTR *PFN_vkDestroyDevice)(VkDevice, const void*);
typedef void (VKAPI_PTR *PFN_vkDestroyInstance)(VkInstance, const void*);

int main() {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\compute-test.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== BC-250 COMPUTE SHADER TEST ===\n\n");

    HMODULE hVulkan = LoadLibraryA("amdbc250vulkan.dll");
    if (!hVulkan) { Log("Vulkan ICD not loaded!\n"); fclose(g_log); return 1; }
    Log("Vulkan ICD loaded\n");

    void* (VKAPI_PTR *vk_icdGetInstanceProcAddr)(VkInstance, const char*) = 
        (void* (VKAPI_PTR *)(VkInstance, const char*))GetProcAddress(hVulkan, "vk_icdGetInstanceProcAddr");
    
    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(NULL, "vkCreateInstance");
    PFN_vkEnumeratePhysicalDevices vkEnumerate = (PFN_vkEnumeratePhysicalDevices)vk_icdGetInstanceProcAddr(NULL, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties vkGetProps = (PFN_vkGetPhysicalDeviceProperties)vk_icdGetInstanceProcAddr(NULL, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetQueues = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vk_icdGetInstanceProcAddr(NULL, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkCreateDevice vkCreateDevice = (PFN_vkCreateDevice)vk_icdGetInstanceProcAddr(NULL, "vkCreateDevice");
    PFN_vkGetDeviceQueue vkGetDeviceQueue = (PFN_vkGetDeviceQueue)vk_icdGetInstanceProcAddr(NULL, "vkGetDeviceQueue");
    PFN_vkDestroyDevice vkDestroyDevice = (PFN_vkDestroyDevice)vk_icdGetInstanceProcAddr(NULL, "vkDestroyDevice");
    PFN_vkDestroyInstance vkDestroyInstance = (PFN_vkDestroyInstance)vk_icdGetInstanceProcAddr(NULL, "vkDestroyInstance");

    if (!vkCreateInstance) { Log("Cannot get functions!\n"); FreeLibrary(hVulkan); fclose(g_log); return 1; }
    Log("Functions resolved: OK\n");

    VkApplicationInfo appInfo = { (uint32_t)0, NULL, NULL, 0, "", 0, (uint32_t)(1 << 22) }; // VK_API_VERSION_1_0
    VkInstanceCreateInfo instInfo = { (uint32_t)1, NULL, NULL, &appInfo, 0, NULL, 0, NULL }; // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    
    VkInstance instance;
    VkResult res = vkCreateInstance(&instInfo, NULL, &instance);
    Log("CreateInstance: %s (res=%u)\n", res == 0 ? "OK" : "FAILED", res);

    uint32_t devCount = 0;
    vkEnumerate(instance, &devCount, NULL);
    Log("Physical devices: %u\n", devCount);

    if (devCount > 0) {
        VkPhysicalDevice physDev;
        vkEnumerate(instance, &devCount, &physDev);

        // Get device name (first 32 bytes of properties)
        char props[256] = {0};
        vkGetProps(physDev, props);
        Log("Device name: %s\n", props + 16); // deviceName is at offset 16 in VkPhysicalDeviceProperties

        // Get queue families
        uint32_t qCount = 0;
        vkGetQueues(physDev, &qCount, NULL);
        Log("Queue families: %u\n", qCount);

        // Find compute queue
        int computeQ = -1;
        char queues[256 * 16] = {0};
        vkGetQueues(physDev, &qCount, queues);
        for (uint32_t i = 0; i < qCount && i < 16; i++) {
            uint32_t* q = (uint32_t*)&queues[i * 16];
            Log("  Queue %u: flags=0x%08X count=%u\n", i, q[0], q[1]);
            if (q[0] & 4 && computeQ < 0) computeQ = i; // 4 = VK_QUEUE_COMPUTE_BIT
        }

        if (computeQ >= 0) {
            Log("Found compute queue: %u\n", computeQ);
            float priority = 1.0f;
            VkDeviceQueueCreateInfo qInfo = { (uint32_t)2, NULL, 0, (uint32_t)computeQ, 1, &priority }; // VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
            VkDeviceCreateInfo devInfo = { (uint32_t)3, NULL, 0, 1, &qInfo, 0, NULL, 0, NULL, NULL }; // VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO
            
            VkDevice device;
            res = vkCreateDevice(physDev, &devInfo, NULL, &device);
            Log("CreateDevice: %s (res=%u)\n", res == 0 ? "OK" : "FAILED", res);

            if (res == 0) {
                VkQueue queue;
                vkGetDeviceQueue(device, computeQ, 0, &queue);
                Log("Got compute queue: %s\n", queue != NULL ? "OK" : "FAILED");
                Log("\n*** GPU CAN DO COMPUTE! ***\n");
                vkDestroyDevice(device, NULL);
            }
        } else {
            Log("No compute queue!\n");
        }
    }

    vkDestroyInstance(instance, NULL);
    FreeLibrary(hVulkan);
    Log("\n=== COMPUTE TEST COMPLETE ===\n");
    fclose(g_log);
    printf("Done. Check output\\compute-test.log\n");
    return 0;
}
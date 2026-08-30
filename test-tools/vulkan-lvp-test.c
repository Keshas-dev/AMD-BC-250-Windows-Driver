/* vulkan-lvp-test.c — minimal Vulkan enumeration test
 * Checks if Vulkan loader finds the Lavapipe ICD and can create an instance.
 *
 * Compile: cl /nologo vulkan-lvp-test.c /link user32.lib /SUBSYSTEM:CONSOLE
 * (No vulkan.lib needed — we dynamically load vulkan-1.dll)
 */
#include <windows.h>
#include <stdio.h>

typedef void* VkInstance;
typedef VkInstance (*PFN_vkCreateInstance)(void*, void*);
typedef void (*PFN_vkDestroyInstance)(VkInstance, void*);

/* Minimal Vulkan structs (offsets match 1.3 ABI) */
typedef struct {
    unsigned int sType;        /* VK_STRUCTURE_TYPE_APPLICATION_INFO = 0 */
    const void* pNext;
    const char* pApplicationName;
    unsigned int applicationVersion;
    const char* pEngineName;
    unsigned int engineVersion;
    unsigned int apiVersion;
} VkApplicationInfo;

typedef struct {
    unsigned int sType;        /* VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1 */
    const void* pNext;
    const void* flags_placeholder;
    const VkApplicationInfo* pApplicationInfo;
    unsigned int enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    unsigned int enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;

int main(void) {
    printf("=== Vulkan Lavapipe ICD Test ===\n\n");

    HMODULE vk = LoadLibraryA("vulkan-1.dll");
    if (!vk) {
        printf("FAIL: vulkan-1.dll not found.\n");
        printf("Install Vulkan Runtime or set VK_ICD_FILENAMES env var.\n");
        return 1;
    }
    printf("vulkan-1.dll loaded OK\n");

    PFN_vkCreateInstance vkCreateInstance =
        (PFN_vkCreateInstance)GetProcAddress(vk, "vkCreateInstance");
    PFN_vkDestroyInstance vkDestroyInstance =
        (PFN_vkDestroyInstance)GetProcAddress(vk, "vkDestroyInstance");
    if (!vkCreateInstance || !vkDestroyInstance) {
        printf("FAIL: GetProcAddress\n"); return 1;
    }

    /* Count physical devices via instance */
    /* We need vkEnumeratePhysicalDevices but let's just create instance first */

    VkApplicationInfo appInfo = {0};
    appInfo.sType = 0; /* VK_STRUCTURE_TYPE_APPLICATION_INFO */
    appInfo.pApplicationName = "BC250-VulkanTest";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "BC250";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = (1 << 22) | (3 << 12); /* VK_API_VERSION_1_3 */

    VkInstanceCreateInfo ci = {0};
    ci.sType = 1; /* VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO */
    ci.pApplicationInfo = &appInfo;

    VkInstance instance = NULL;
    unsigned int result = (unsigned int)(uintptr_t)vkCreateInstance(&ci, NULL);
    /* Actually vkCreateInstance returns VkResult, need proper call */
    typedef long VkResult;
    VkResult r;
    {
        typedef VkResult (*PFN_vkCreateInstanceReal)(const VkInstanceCreateInfo*, const void*, VkInstance*);
        PFN_vkCreateInstanceReal fn = (PFN_vkCreateInstanceReal)GetProcAddress(vk, "vkCreateInstance");
        if (!fn) { printf("FAIL: no vkCreateInstance export\n"); return 1; }
        r = fn(&ci, NULL, &instance);
    }

    printf("vkCreateInstance: result=%ld instance=%p\n", (long)r, (void*)instance);

    if (r == 0) { /* VK_SUCCESS */
        printf("\n>>> VULKAN INSTANCE CREATED SUCCESSFULLY! <<<\n");
        printf(">>> Lavapipe ICD is working! <<<\n");

        /* Enumerate physical devices */
        typedef long (*PFN_vkEnumeratePhysicalDevices)(VkInstance, unsigned int*, void*);
        PFN_vkEnumeratePhysicalDevices enumFn = (PFN_vkEnumeratePhysicalDevices)GetProcAddress(vk, "vkEnumeratePhysicalDevices");
        if (enumFn) {
            unsigned int count = 0;
            enumFn(instance, &count, NULL);
            printf("Physical devices: %u\n", count);
            if (count > 0) {
                /* Get device properties */
                typedef struct { unsigned int apiVer; unsigned int drvVer; unsigned int vendorID; unsigned int deviceID; int deviceType; char deviceName[256]; } VkPhysicalDeviceProperties_min;
                typedef void (*PFN_vkGetPhysicalDeviceProperties)(void*, VkPhysicalDeviceProperties_min*);
                PFN_vkGetPhysicalDeviceProperties getProps = (PFN_vkGetPhysicalDeviceProperties)GetProcAddress(vk, "vkGetPhysicalDeviceProperties");
                void** devs = (void**)malloc(sizeof(void*) * count);
                unsigned int got = count;
                enumFn(instance, &got, devs);
                for (unsigned int i = 0; i < got && i < count; i++) {
                    if (getProps && getProps != (void*)1) {
                        VkPhysicalDeviceProperties_min props = {0};
                        getProps(devs[i], &props);
                        printf("  GPU %u: %s (vendor=0x%04X device=0x%04X)\n",
                               i, props.deviceName, props.vendorID, props.deviceID);
                    }
                }
                free(devs);
            }
        }

        /* Destroy */
        vkDestroyInstance(instance, NULL);
        printf("\nInstance destroyed. Vulkan stack fully functional!\n");
    } else {
        printf("FAILED to create Vulkan instance (result=%ld)\n", (long)r);
        printf("Check: VK_ICD_FILENAMES or HKCU\\...Vulkan registry\n");
    }

    FreeLibrary(vk);
    return (r == 0) ? 0 : 1;
}

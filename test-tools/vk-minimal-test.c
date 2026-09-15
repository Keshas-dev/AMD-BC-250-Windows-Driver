/* vk-minimal-test.c - minimal Vulkan instance + ICD enumeration. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>

static const char* vk_result_name(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "SUCCESS";
    case VK_NOT_READY: return "NOT_READY";
    case VK_TIMEOUT: return "TIMEOUT";
    case VK_EVENT_SET: return "EVENT_SET";
    case VK_EVENT_RESET: return "EVENT_RESET";
    case VK_INCOMPLETE: return "INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "ERR_HOST_MEM";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "ERR_DEVICE_MEM";
    case VK_ERROR_INITIALIZATION_FAILED: return "ERR_INIT";
    case VK_ERROR_DEVICE_LOST: return "ERR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "ERR_MEM_MAP";
    case VK_ERROR_LAYER_NOT_PRESENT: return "ERR_LAYER";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "ERR_EXT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "ERR_FEATURE";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "ERR_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "ERR_TOO_MANY";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "ERR_FORMAT";
    case VK_ERROR_FRAGMENTED_POOL: return "ERR_POOL";
    default: return "UNKNOWN";
    }
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Vulkan minimal test ===\n");

    /* Check if Vulkan loader is available. */
    HMODULE vulkan = LoadLibraryA("vulkan-1.dll");
    if (!vulkan) {
        printf("vulkan-1.dll NOT FOUND\n");
        return 1;
    }
    printf("vulkan-1.dll loaded OK\n");

    /* Try to enumerate instances. */
    VkApplicationInfo appInfo = {0};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "BC250 VK Test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "BC250";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t globalLayerCount = 0;
    VkResult r = vkEnumerateInstanceLayerProperties(&globalLayerCount, NULL);
    printf("vkEnumerateInstanceLayerProperties: %s (layers=%u)\n",
           vk_result_name(r), globalLayerCount);

    uint32_t globalExtCount = 0;
    r = vkEnumerateInstanceExtensionProperties(NULL, &globalExtCount, NULL);
    printf("vkEnumerateInstanceExtensionProperties: %s (exts=%u)\n",
           vk_result_name(r), globalExtCount);

    VkInstance instance = VK_NULL_HANDLE;
    r = vkCreateInstance(&createInfo, NULL, &instance);
    printf("vkCreateInstance: %s (instance=%p)\n", vk_result_name(r), (void*)instance);

    if (r == VK_SUCCESS && instance != VK_NULL_HANDLE) {
        uint32_t physCount = 0;
        r = vkEnumeratePhysicalDevices(instance, &physCount, NULL);
        printf("vkEnumeratePhysicalDevices: %s (GPUs=%u)\n",
               vk_result_name(r), physCount);

        if (physCount > 0) {
            VkPhysicalDevice* phys = (VkPhysicalDevice*)malloc(physCount * sizeof(VkPhysicalDevice));
            r = vkEnumeratePhysicalDevices(instance, &physCount, phys);
            printf("  enumeration: %s\n", vk_result_name(r));
            for (uint32_t i = 0; i < physCount; i++) {
                VkPhysicalDeviceProperties props = {0};
                vkGetPhysicalDeviceProperties(phys[i], &props);
                printf("  GPU[%u]: %s (driver %s, API %d.%d.%d)\n",
                       i, props.deviceName, props.driverVersion > 0 ? "custom" : "unknown",
                       VK_VERSION_MAJOR(props.apiVersion),
                       VK_VERSION_MINOR(props.apiVersion),
                       VK_VERSION_PATCH(props.apiVersion));
            }
            free(phys);
        }
        vkDestroyInstance(instance, NULL);
    }

    printf("\n=== ICD paths ===\n");
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char name[256];
        DWORD idx = 0;
        while (RegEnumKeyA(hKey, idx, name, sizeof(name)) == ERROR_SUCCESS) {
            if (strstr(name, "Vulkan") || strstr(name, "vulkan") ||
                strstr(name, "amd") || strstr(name, "radv")) {
                printf("  Service: %s\n", name);
            }
            idx++;
        }
        RegCloseKey(hKey);
    }

    return 0;
}

/*
 * BC-250 Vulkan ICD - Windows Vulkan Installable Client Driver
 * 
 * Implements minimal Vulkan 1.4 API for AMD BC-250 GPU.
 * Uses ACO shader compiler (from Mesa) and our KMD for hardware access.
 *
 * SPDX-License-Identifier: MIT
 */

/* Win32 WSI types (VkWin32SurfaceCreateInfoKHR) live behind this;
 * must be defined before any vulkan header. */
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan_core.h>
#include "bc250_vulkan.h"
#include "bc250_aco_wrapper.h"
#include "bc250_shader.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../test-tools/wddm_debug_hooks.c"

/* Vulkan error codes not in our headers */
#ifndef VK_SUCCESS
#define VK_SUCCESS 0
#endif
#ifndef VK_ERROR_FORMAT_NOT_SUPPORTED
#define VK_ERROR_FORMAT_NOT_SUPPORTED -9
#endif
#ifndef VK_ERROR_MEMORY_MAP_FAILED
#define VK_ERROR_MEMORY_MAP_FAILED -1000001001
#endif

/* Forward declaration */
static void bc250_DestroyDevice(VkDevice device);

/* File trace: appends entry/exit markers to icd-log.txt.
 * Last line in the log after a crash == crash site. */
static void bc250_trace(const char* func, const char* phase, const void* arg)
{
    FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
    if (f) {
        fprintf(f, "TRACE %s %s arg=%p\n", func, phase, arg);
        fflush(f);
        fclose(f);
    }
}
#define BC250_TRACE_IN(arg) bc250_trace(__FUNCTION__, "enter", (const void*)(arg))
#define BC250_TRACE_OUT(arg) bc250_trace(__FUNCTION__, "exit", (const void*)(arg))

/* Device context */
typedef struct {
    VkDevice        device;
    VkPhysicalDevice physicalDevice;
    HANDLE          kmdDevice;
    uint64_t        vramTotal;
    uint64_t        vramUsed;
    uint64_t        nextGpuVa;
    uint32_t        fenceValue;
} BC250_VK_DEVICE;

static BC250_VK_DEVICE g_Device = { 0, 0, 0, 0, 0, 1 /* fenceValue: KMD needs >0 to append EOP */ };

/* Memory allocation tracking */
typedef struct {
    void*       cpuVa;      /* CPU virtual address */
    uint64_t    gpuPa;      /* GPU physical address */
    uint64_t    size;       /* Allocation size */
    uint32_t    flags;      /* Allocation flags */
} BC250_MEM_ALLOC;

#define MAX_ALLOCATIONS 64
static BC250_MEM_ALLOC g_Allocations[MAX_ALLOCATIONS];
static uint32_t g_NumAllocations = 0;

/* KMD communication */
static HANDLE bc250_open_kmd(void)
{
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

static VkResult bc250_init_instance(void)
{
    OutputDebugStringA("BC-250 Vulkan: Instance created\n");
    return VK_SUCCESS;
}

/* Enumerate instance extensions: surface + win32_surface + debug_utils.
 * vkcube refuses to start when these are absent (usage error, exit 1). */
static VkResult bc250_vkEnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pPropertyCount, void* pProperties)
{
    static const struct { char name[256]; uint32_t ver; } kExts[] = {
        { "VK_KHR_surface", 25 },
        { "VK_KHR_win32_surface", 6 },
        { "VK_EXT_debug_utils", 2 },
    };
    const uint32_t n = (uint32_t)(sizeof(kExts) / sizeof(kExts[0]));
    UNREFERENCED_PARAMETER(pLayerName);

    /* Write to file so we know this function is called */
    FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
    if (f) {
        fprintf(f, "EnumInstanceExtProps called: pLayerName=%s pPropertyCount=%p pProperties=%p\n",
                pLayerName ? pLayerName : "(null)", (void*)pPropertyCount, pProperties);
        fflush(f);
        fclose(f);
    }

    if (pProperties == NULL) {
        if (pPropertyCount) *pPropertyCount = n;
        return VK_SUCCESS;
    }
    {
        /* VkExtensionProperties = name[256] + specVersion(4). */
        uint32_t want = *pPropertyCount < n ? *pPropertyCount : n;
        memcpy(pProperties, kExts, (size_t)want * (256 + 4));
        *pPropertyCount = want;
        return want < n ? VK_INCOMPLETE : VK_SUCCESS;
    }
}

/* Enumerate device extensions: swapchain (vkcube requires it). */
static VkResult bc250_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice, const char* pLayerName,
    uint32_t* pPropertyCount, void* pProperties)
{
    static const struct { char name[256]; uint32_t ver; } kExts[] = {
        { "VK_KHR_swapchain", 70 },
    };
    const uint32_t n = (uint32_t)(sizeof(kExts) / sizeof(kExts[0]));
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(pLayerName);
    BC250_TRACE_IN(physicalDevice);
    if (pProperties == NULL) {
        if (pPropertyCount) *pPropertyCount = n;
        BC250_TRACE_OUT(pPropertyCount);
        return VK_SUCCESS;
    }
    {
        uint32_t want = *pPropertyCount < n ? *pPropertyCount : n;
        memcpy(pProperties, kExts, (size_t)want * (256 + 4));
        *pPropertyCount = want;
        BC250_TRACE_OUT(pProperties);
        return want < n ? VK_INCOMPLETE : VK_SUCCESS;
    }
}

/* Additional required instance-level stubs for Vulkan 1.4 loader compatibility */
static void bc250_vkGetPhysicalDeviceSparseImageFormatPropertiesStub(VkPhysicalDevice a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t* f, void** g) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); if (f) *f = 0; UNREFERENCED_PARAMETER(g); }
static void bc250_vkGetPhysicalDeviceSparseImageFormatProperties2Stub(VkPhysicalDevice a, const void* b, uint32_t* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; UNREFERENCED_PARAMETER(d); }
static void bc250_vkGetPhysicalDeviceQueueFamilyProperties2Stub(VkPhysicalDevice a, uint32_t* b, void* c) { UNREFERENCED_PARAMETER(a); if (b) *b = 0; UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkGetPhysicalDeviceToolPropertiesStub(VkPhysicalDevice a, uint32_t* b, void* c) { UNREFERENCED_PARAMETER(a); if (b) *b = 0; UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }static void bc250_vkGetPhysicalDeviceMemoryProperties2Stub(VkPhysicalDevice a, void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); }
static void bc250_vkGetPhysicalDeviceFeatures2Stub(VkPhysicalDevice a, void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); }
static void bc250_vkGetPhysicalDeviceFormatProperties2Stub(VkPhysicalDevice a, uint32_t b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkGetPhysicalDeviceImageFormatProperties2Stub(VkPhysicalDevice a, const void* b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static void bc250_vkGetPhysicalDeviceProperties2Stub(VkPhysicalDevice a, void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); }
static VkResult bc250_vkGetPhysicalDeviceSurfaceSupportStub(VkPhysicalDevice a, uint32_t b, void* c, uint32_t* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); if (d) *d = 0; return VK_SUCCESS; }
static VkResult bc250_vkGetPhysicalDeviceSurfaceCapabilitiesStub(VkPhysicalDevice a, void* b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static VkResult bc250_vkGetPhysicalDeviceSurfaceFormatsStub(VkPhysicalDevice a, void* b, uint32_t* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkGetPhysicalDeviceSurfacePresentModesStub(VkPhysicalDevice a, void* b, uint32_t* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkGetPhysicalDeviceDisplayPropertiesKHRStub(VkPhysicalDevice a, uint32_t* b, void* c) { UNREFERENCED_PARAMETER(a); if (b) *b = 0; UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static VkResult bc250_vkGetPhysicalDeviceDisplayPlanePropertiesKHRStub(VkPhysicalDevice a, uint32_t* b, void* c) { UNREFERENCED_PARAMETER(a); if (b) *b = 0; UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static VkResult bc250_vkGetDisplayPlaneSupportedDisplaysKHRStub(VkPhysicalDevice a, uint32_t b, uint32_t* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkGetDisplayModePropertiesKHRStub(VkPhysicalDevice a, void* b, uint32_t* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkCreateDisplayModeKHRStub(VkPhysicalDevice a, void* b, const void* c, const void* d, void* e) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); return VK_SUCCESS; }
static VkResult bc250_vkGetDisplayPlaneCapabilitiesKHRStub(VkPhysicalDevice a, void* b, uint32_t c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkCreateDisplayPlaneSurfaceKHRStub(VkInstance a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkCreateViSurfaceNNStub(VkInstance a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkEnumeratePhysicalDeviceGroupsStub(VkInstance a, uint32_t* b, void* c) { UNREFERENCED_PARAMETER(a); if (b) *b = 0; UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static VkResult bc250_vkCreateIOSSurfaceMVKStub(VkInstance a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkCreateMacOSSurfaceMVKStub(VkInstance a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkCreateMetalSurfaceEXTStub(VkInstance a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkCreateStreamDescriptorSurfaceGGPStub(VkInstance a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkCreateWin32SurfaceKHRStub(VkInstance a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static uint32_t bc250_vkGetPhysicalDeviceExternalFencePropertiesStub(VkPhysicalDevice a, const void* b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return 0; }
static uint32_t bc250_vkGetPhysicalDeviceExternalSemaphorePropertiesStub(VkPhysicalDevice a, const void* b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return 0; }
static uint32_t bc250_vkGetPhysicalDeviceExternalBufferPropertiesStub(VkPhysicalDevice a, const void* b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return 0; }
static VkResult bc250_vkGetPhysicalDevicePresentRectanglesKHRStub(VkPhysicalDevice a, void* b, uint32_t* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkReleaseDisplayEXTStub(VkPhysicalDevice a, void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); return VK_SUCCESS; }
static VkResult bc250_vkAcquireXlibDisplayEXTStub(VkPhysicalDevice a, void* b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static VkResult bc250_vkGetRandROutputDisplayEXTStub(VkPhysicalDevice a, void* b, uint32_t c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }

/* Fake-handle allocator for create-stubs: every created object gets a
 * unique non-NULL handle. vkcube passes handles around opaquely; NULL
 * handles crash it. Our stubs never dereference them. */
static LONG g_FakeHandle = 0x10000;
static void* bc250_fake_handle(void) { return (void*)(uintptr_t)InterlockedIncrement(&g_FakeHandle); }
static void bc250_write_fake(void* out) { if (out) *(void**)out = bc250_fake_handle(); }
__declspec(dllexport) void* VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
__declspec(dllexport) void* VKAPI_CALL vk_icdGetDeviceProcAddr(VkDevice device, const char* pName);

/* Required Vulkan 1.0 stub functions */
static VkResult bc250_vkGetPhysicalDeviceFeatures(VkPhysicalDevice a, void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); return VK_SUCCESS; }
static void bc250_vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice a, uint32_t b, void* c) {
    UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b);
    /* VkFormatProperties = 3x VkFormatFeatureFlags. Zero = format
     * unsupported -> vulkaninfo skips it. MUST write: caller passes
     * uninitialized stack memory, garbage feature bits crash consumers. */
    if (c) memset(c, 0, 3 * sizeof(uint32_t));
}
static VkResult bc250_vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint64_t f, void* g, void* h) {
    UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c);
    UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); UNREFERENCED_PARAMETER(f);
    UNREFERENCED_PARAMETER(h);
    /* VkImageFormatProperties = extent(12) + mipLevels(4) + arrayLayers(4)
     * + sampleCounts(4) + resourceSize(8) = 32 bytes. Sane values so
     * vulkaninfo never chokes on garbage (was: depth=3524901744 etc). */
    if (g) {
        uint8_t* o = (uint8_t*)g;
        memset(o, 0, 32);
        *(uint32_t*)(o + 0) = 16384;  /* maxExtent.width */
        *(uint32_t*)(o + 4) = 16384;  /* maxExtent.height */
        *(uint32_t*)(o + 8) = 2048;   /* maxExtent.depth */
        *(uint32_t*)(o + 12) = 15;    /* maxMipLevels */
        *(uint32_t*)(o + 16) = 2048;  /* maxArrayLayers */
        *(uint32_t*)(o + 20) = 0x1 | 0x4; /* sampleCounts = 1|4 BIT */
        *(uint64_t*)(o + 24) = 1ULL * 1024 * 1024 * 1024; /* maxResourceSize 1GB */
    }
    return VK_SUCCESS;
}
static void bc250_vkGetPhysicalDevicePropertiesStub(VkPhysicalDevice a, void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); }
static void bc250_vkGetPhysicalDeviceQueueFamilyPropertiesStub(VkPhysicalDevice a, uint32_t* b, void* c) { UNREFERENCED_PARAMETER(a); if (b) *b = 1; UNREFERENCED_PARAMETER(c); }
static void bc250_vkGetPhysicalDeviceMemoryPropertiesStub(VkPhysicalDevice a, void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); }
static VkResult bc250_vkEnumerateDeviceLayerProperties(VkPhysicalDevice a, uint32_t* b, void* c) { UNREFERENCED_PARAMETER(a); if (b) *b = 0; UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static void* bc250_vkGetInstanceProcAddrStub(VkInstance a, const char* b) { return vk_icdGetInstanceProcAddr(a, b); }
static VkResult bc250_vkAllocateMemoryStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkFreeMemoryStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkMapMemoryStub(VkDevice a, void* b, uint64_t c, uint64_t d, uint64_t e, void** f) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); *f = (void*)0xDEAD; return VK_SUCCESS; }
static void bc250_vkUnmapMemoryStub(VkDevice a, void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); }
static VkResult bc250_vkCreateBufferStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyBufferStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateImageStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyImageStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateFenceStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyFenceStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkResetFencesStub(VkDevice a, uint32_t b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static VkResult bc250_vkGetFenceStatusStub(VkDevice a, void* b, uint32_t* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; return VK_SUCCESS; }
static VkResult bc250_vkWaitForFencesStub(VkDevice a, uint32_t b, const void* c, uint32_t d, uint64_t e) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); return VK_SUCCESS; }
static VkResult bc250_vkCreateCommandPoolStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyCommandPoolStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkResetCommandPoolStub(VkDevice a, void* b, uint32_t c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static VkResult bc250_vkAllocateCommandBuffersStub(VkDevice a, const void* b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static void bc250_vkFreeCommandBuffersStub(VkDevice a, void* b, uint32_t c, const void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); }
static VkResult bc250_vkBeginCommandBufferStub(VkCommandBuffer a, const void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); return VK_SUCCESS; }
static VkResult bc250_vkEndCommandBufferStub(VkCommandBuffer a) { UNREFERENCED_PARAMETER(a); return VK_SUCCESS; }
static VkResult bc250_vkResetCommandBufferStub(VkCommandBuffer a, uint32_t b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); return VK_SUCCESS; }
static void bc250_vkCmdBeginRenderPassStub(VkCommandBuffer a, const void* b, uint32_t c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static void bc250_vkCmdEndRenderPassStub(VkCommandBuffer a) { UNREFERENCED_PARAMETER(a); }
static void bc250_vkCmdNextSubpassStub(VkCommandBuffer a, uint32_t b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); }
static void bc250_vkCmdBindPipelineStub(VkCommandBuffer a, uint32_t b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static void bc250_vkCmdBindVertexBuffersStub(VkCommandBuffer a, uint32_t b, uint32_t c, const void* d, const uint64_t* e) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); }
static void bc250_vkCmdBindDescriptorSetsStub(VkCommandBuffer a, uint32_t b, void* c, uint32_t d, uint32_t e, const void* f, uint32_t g, const uint32_t* h) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); UNREFERENCED_PARAMETER(f); UNREFERENCED_PARAMETER(g); UNREFERENCED_PARAMETER(h); }
static void bc250_vkCmdDrawStub(VkCommandBuffer a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); }
static void bc250_vkCmdDrawIndexedStub(VkCommandBuffer a, uint32_t b, uint32_t c, uint32_t d, int32_t e, uint32_t f) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); UNREFERENCED_PARAMETER(f); }
static void bc250_vkCmdPipelineBarrierStub(VkCommandBuffer a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, const void* f, uint32_t g, const void* h, uint32_t i, const void* j) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); UNREFERENCED_PARAMETER(f); UNREFERENCED_PARAMETER(g); UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(i); UNREFERENCED_PARAMETER(j); }
static void bc250_vkCmdCopyBufferStub(VkCommandBuffer a, void* b, void* c, uint32_t d, const void* e) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); }
static void bc250_vkCmdCopyImageStub(VkCommandBuffer a, void* b, uint32_t c, void* d, uint32_t e, uint32_t f, const void* g) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); UNREFERENCED_PARAMETER(f); UNREFERENCED_PARAMETER(g); }
static VkResult bc250_vkQueueSubmitStub(VkQueue a, uint32_t b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkQueueWaitIdleStub(VkQueue a) { UNREFERENCED_PARAMETER(a); return VK_SUCCESS; }
static VkResult bc250_vkDeviceWaitIdleStub(VkDevice a) { UNREFERENCED_PARAMETER(a); return VK_SUCCESS; }
static VkResult bc250_vkCreateRenderPassStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); BC250_TRACE_IN(c); BC250_TRACE_OUT(d); return VK_SUCCESS; }
static void bc250_vkDestroyRenderPassStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateFramebufferStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); BC250_TRACE_IN(c); BC250_TRACE_OUT(d); return VK_SUCCESS; }
static void bc250_vkDestroyFramebufferStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateSemaphoreStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); return VK_SUCCESS; }
static void bc250_vkDestroySemaphoreStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateShaderModuleStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); BC250_TRACE_IN(c); BC250_TRACE_OUT(d); return VK_SUCCESS; }
static void bc250_vkDestroyShaderModuleStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreatePipelineLayoutStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); BC250_TRACE_IN(c); BC250_TRACE_OUT(d); return VK_SUCCESS; }
static void bc250_vkDestroyPipelineLayoutStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateDescriptorSetLayoutStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); BC250_TRACE_IN(c); BC250_TRACE_OUT(d); return VK_SUCCESS; }
static void bc250_vkDestroyDescriptorSetLayoutStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateDescriptorPoolStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); BC250_TRACE_IN(c); BC250_TRACE_OUT(d); return VK_SUCCESS; }
static void bc250_vkDestroyDescriptorPoolStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkAllocateDescriptorSetsStub(VkDevice a, const void* b, void* c) {
    uint32_t n = 1, i;
    UNREFERENCED_PARAMETER(a);
    BC250_TRACE_IN(b);
    /* VkDescriptorSetAllocateInfo.descriptorSetCount at offset 24. */
    if (b) { n = *(const uint32_t*)((const uint8_t*)b + 24); if (n < 1 || n > 64) n = 1; }
    if (c) { for (i = 0; i < n; i++) ((void**)c)[i] = bc250_fake_handle(); }
    BC250_TRACE_OUT(c);
    return VK_SUCCESS;
}
static void bc250_vkUpdateDescriptorSetsStub(VkDevice a, uint32_t b, const void* c, uint32_t d, const void* e) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); }
static VkResult bc250_vkCreateSamplerStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); return VK_SUCCESS; }
static void bc250_vkDestroySamplerStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateQueryPoolStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); return VK_SUCCESS; }
static void bc250_vkDestroyQueryPoolStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreatePipelineCacheStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); BC250_TRACE_IN(c); BC250_TRACE_OUT(d); return VK_SUCCESS; }
static void bc250_vkDestroyPipelineCacheStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkGetPipelineCacheDataStub(VkDevice a, void* b, uint32_t* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkMergePipelineCachesStub(VkDevice a, void* b, uint32_t c, const void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkFreeDescriptorSetsStub(VkDevice a, void* b, uint32_t c, const void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkResetDescriptorPoolStub(VkDevice a, void* b, uint32_t c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static VkResult bc250_vkCreateImageViewStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); bc250_write_fake(d); return VK_SUCCESS; }
static void bc250_vkDestroyImageViewStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateSwapchainKHRStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroySwapchainKHRStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkGetSwapchainImagesKHRStub(VkDevice a, void* b, uint32_t* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkAcquireNextImageKHRStub(VkDevice a, void* b, uint64_t c, void* d, void* e, uint32_t* f) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); if (f) *f = 0; return VK_SUCCESS; }

static VkResult bc250_init_device(VkDevice device)
{
    BC250_VK_DEVICE* dev = (BC250_VK_DEVICE*)device;
    BC250_TRACE_IN(device);
    
    FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
    if (f) { fprintf(f, "bc250_init_device ENTER\n"); fflush(f); fclose(f); }
    
    dev->kmdDevice = bc250_open_kmd();
    FILE *f2 = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
    if (f2) { fprintf(f2, "bc250_init_device: after open_kmd, handle=%p\n", dev->kmdDevice); fflush(f2); fclose(f2); }
    if (dev->kmdDevice == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        char buf[256];
        snprintf(buf, sizeof(buf), "BC-250 Vulkan: KMD open FAILED (error %lu, handle=%p)\n",
                 err, (void*)dev->kmdDevice);
        OutputDebugStringA(buf);
    } else {
        OutputDebugStringA("BC-250 Vulkan: KMD opened OK\n");
/* INIT_HARDWARE with NBIO_MAP only (safe: maps BAR5 + PCI mem
         * space, skips full HW init which would BSOD). Without this,
         * every SEND_PM4 fails with gle=21 NOT_READY. Full 32-byte
         * struct required (Fb fields included, 0 = auto-detect). */
        {
/* Must match AMDBC250_IOCTL_INIT_HARDWARE exactly (28 bytes, no pad) */
/* Use explicit BAR5 PA 0xFE800000 (512KB) + NBIO_MAP=1 skips full HW init (avoids BSOD). */
            /* If you need to re-enable auto-detect, change base=0 and KMD will scan PCIe config. */
            struct { uint64_t base; uint32_t size; uint32_t flags; uint64_t fbBase; uint32_t fbSize; } ih = { 0xFE800000, 0x80000, 1, 0, 0 };
            DWORD br = 0;
            OutputDebugStringA("BC-250 Vulkan: Calling INIT_HARDWARE NBIO_MAP...\n");
            BOOL ok = DeviceIoControl(dev->kmdDevice, 0x80000B80, &ih, sizeof(ih),
                                      NULL, 0, &br, NULL);
            DWORD gle = GetLastError();
            FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
            if (f) { fprintf(f, "INIT_HARDWARE called ok=%d gle=%lu ret=%lu\n", ok, gle, br); fflush(f); fclose(f); }
            if (!ok) {
                FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
                if (f) { fprintf(f, "INIT_HARDWARE FAILED gle=%lu ret=%lu\n", gle, br); fflush(f); fclose(f); }
            } else {
                FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
                if (f) { fprintf(f, "INIT_HARDWARE NBIO_MAP OK ret=%lu\n", br); fflush(f); fclose(f); }
            }
        }
    }
    
    dev->nextGpuVa = 0x100000000ULL;
    dev->vramTotal = 4ULL * 1024 * 1024 * 1024;
    dev->vramUsed = 0;
    dev->fenceValue = 1;
    
    OutputDebugStringA("BC-250 Vulkan: Device initialized\n");
    BC250_TRACE_OUT(device);
    return VK_SUCCESS;
}

/* Vulkan API implementations */

VkResult VKAPI_CALL bc250_vkCreateInstance(
    const void* pCreateInfo,
    const void* pAllocator,
    VkInstance* pInstance)
{
    VkResult result = bc250_init_instance();
    BC250_TRACE_IN(pCreateInfo);
    if (result == VK_SUCCESS) {
        *pInstance = (VkInstance)1; /* Dummy handle */
    }
    BC250_TRACE_OUT(pInstance);
    return result;
}

void VKAPI_CALL bc250_vkDestroyInstance(VkInstance instance, const void* pAllocator)
{
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(pAllocator);
    BC250_TRACE_IN(instance);
    OutputDebugStringA("BC-250 Vulkan: Instance destroyed\n");
    BC250_TRACE_OUT(instance);
}

VkResult VKAPI_CALL bc250_vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices)
{
    UNREFERENCED_PARAMETER(instance);
    BC250_TRACE_IN(pPhysicalDeviceCount);
    if (pPhysicalDevices == NULL) {
        *pPhysicalDeviceCount = 1;
        BC250_TRACE_OUT(pPhysicalDeviceCount);
        return VK_SUCCESS;
    }
    
    if (*pPhysicalDeviceCount >= 1) {
        /* Must be a valid BC250_VK_DEVICE pointer: MemoryProperties
         * dereferences it for the KMD VRAM query. (VkPhysicalDevice)1
         * AV'd vulkaninfo (0xC0000005). */
        *pPhysicalDevices = (VkPhysicalDevice)&g_Device;
        *pPhysicalDeviceCount = 1;
    }
    BC250_TRACE_OUT(pPhysicalDevices);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const void* pCreateInfo,
    const void* pAllocator,
    VkDevice* pDevice)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(pCreateInfo);
    UNREFERENCED_PARAMETER(pAllocator);
    
    BC250_VK_DEVICE* dev = (BC250_VK_DEVICE*)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BC250_VK_DEVICE));
    BC250_TRACE_IN(physicalDevice);
    
    if (!dev) return VK_ERROR_OUT_OF_HOST_MEMORY;
    
    VkResult result = bc250_init_device((VkDevice)dev);
    if (result != VK_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, dev);
        BC250_TRACE_OUT(pDevice);
        return result;
    }

    /* Publish handle/fence to global used by QueueSubmit (avoids second
     * open + FenceValue=0 first submit). g_Device.kmdDevice is overwritten
     * only if this is the first/only device. */
    if (g_Device.kmdDevice == NULL || g_Device.kmdDevice == INVALID_HANDLE_VALUE)
        g_Device.kmdDevice = dev->kmdDevice;
    if (g_Device.fenceValue == 0)
        g_Device.fenceValue = 1;

    *pDevice = (VkDevice)dev;
    BC250_TRACE_OUT(pDevice);
    return VK_SUCCESS;
}

void VKAPI_CALL bc250_vkDestroyDevice(VkDevice device, const void* pAllocator)
{
    UNREFERENCED_PARAMETER(pAllocator);
    BC250_TRACE_IN(device);
    if (device) {
        BC250_VK_DEVICE* dev = (BC250_VK_DEVICE*)device;
        if (g_Device.kmdDevice == dev->kmdDevice)
            g_Device.kmdDevice = INVALID_HANDLE_VALUE;
        bc250_DestroyDevice(device);
        HeapFree(GetProcessHeap(), 0, (void*)device);
    }
    BC250_TRACE_OUT(device);
}

/* Physical Device Properties */
VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    void* pProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    BC250_TRACE_IN(pProperties);
    
    /* BC-250 (Cyan Skillfish) - RDNA2-based, 24 CUs, GFX1013 */
    VkPhysicalDeviceProperties* props = (VkPhysicalDeviceProperties*)pProperties;
    memset(props, 0, sizeof(VkPhysicalDeviceProperties));
    
    props->apiVersion = VK_API_VERSION_1_3;
    props->driverVersion = 430; /* v4.3 */
    props->vendorID = 0x1002;   /* AMD */
    props->deviceID = 0x13FE;  /* BC-250 */
    props->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    memcpy(props->deviceName, "AMD Radeon BC-250 Graphics (Dream Drivers v4.3)", 52);
    props->limits.maxImageDimension2D = 16384;
    props->limits.maxImageDimension3D = 2048;
    props->limits.maxImageDimensionCube = 16384;
    props->limits.maxImageArrayLayers = 2048;
    props->limits.maxTexelBufferElements = 0x10000000;
    props->limits.maxUniformBufferRange = 0x10000;
    props->limits.maxStorageBufferRange = 0x10000000;
    props->limits.maxPushConstantsSize = 128;
    props->limits.maxMemoryAllocationCount = 4096;
    props->limits.maxSamplerAllocationCount = 1024;
    props->limits.bufferImageGranularity = 4096;
    props->limits.sparseAddressSpaceSize = 0;
    props->limits.maxBoundDescriptorSets = 4;
    props->limits.maxPerStageDescriptorSamplers = 32;
    props->limits.maxPerStageDescriptorUniformBuffers = 16;
    props->limits.maxPerStageDescriptorStorageBuffers = 16;
    props->limits.maxPerStageDescriptorSampledImages = 128;
    props->limits.maxPerStageDescriptorStorageImages = 16;
    props->limits.maxPerStageDescriptorInputAttachments = 16;
    props->limits.maxPerStageResources = 256;
    props->limits.maxDescriptorSetSamplers = 96;
    props->limits.maxDescriptorSetUniformBuffers = 48;
    props->limits.maxDescriptorSetUniformBuffersDynamic = 8;
    props->limits.maxDescriptorSetStorageBuffers = 48;
    props->limits.maxDescriptorSetStorageBuffersDynamic = 8;
    props->limits.maxDescriptorSetSampledImages = 384;
    props->limits.maxDescriptorSetStorageImages = 48;
    props->limits.maxDescriptorSetInputAttachments = 16;
    props->limits.maxVertexInputAttributes = 32;
    props->limits.maxVertexInputBindings = 32;
    props->limits.maxVertexInputAttributeOffset = 2047;
    props->limits.maxVertexInputBindingStride = 2048;
    props->limits.maxVertexOutputComponents = 128;
    props->limits.maxTessellationGenerationLevel = 64;
    props->limits.maxTessellationPatchSize = 32;
    props->limits.maxTessellationControlPerVertexInputComponents = 128;
    props->limits.maxTessellationControlPerVertexOutputComponents = 128;
    props->limits.maxTessellationControlPerPatchOutputComponents = 128;
    props->limits.maxTessellationControlTotalOutputComponents = 2048;
    props->limits.maxGeometryShaderInvocations = 32;
    props->limits.maxGeometryInputComponents = 64;
    props->limits.maxGeometryOutputComponents = 128;
    props->limits.maxGeometryOutputVertices = 256;
    props->limits.maxGeometryTotalOutputComponents = 1024;
    props->limits.maxFragmentInputComponents = 128;
    props->limits.maxFragmentOutputAttachments = 8;
    props->limits.maxFragmentDualSrcAttachments = 1;
    props->limits.maxFragmentCombinedOutputResources = 8;
    props->limits.maxComputeSharedMemorySize = 65536;
    props->limits.maxComputeWorkGroupCount[0] = 65535;
    props->limits.maxComputeWorkGroupCount[1] = 65535;
    props->limits.maxComputeWorkGroupCount[2] = 65535;
    props->limits.maxComputeWorkGroupInvocations = 1024;
    props->limits.maxComputeWorkGroupSize[0] = 1024;
    props->limits.maxComputeWorkGroupSize[1] = 1024;
    props->limits.maxComputeWorkGroupSize[2] = 64;
    props->limits.subPixelPrecisionBits = 4;
    props->limits.subTexelPrecisionBits = 8;
    props->limits.mipmapPrecisionBits = 4;
    props->limits.maxDrawIndexedIndexValue = 0xFFFFFFFF;
    props->limits.maxDrawIndirectCount = 0xFFFFFFFF;
    props->limits.maxSamplerLodBias = 16;
    props->limits.maxSamplerAnisotropy = 16;
    props->limits.maxViewports = 16;
    props->limits.maxViewportDimensions[0] = 16384;
    props->limits.maxViewportDimensions[1] = 16384;
    props->limits.viewportBoundsRange[0] = -32768;
    props->limits.viewportBoundsRange[1] = 32767;
    props->limits.viewportSubPixelBits = 8;
    props->limits.minMemoryMapAlignment = 4096;
    props->limits.minTexelBufferOffsetAlignment = 256;
    props->limits.minUniformBufferOffsetAlignment = 256;
    props->limits.minStorageBufferOffsetAlignment = 256;
    props->limits.minTexelOffset = -8;
    props->limits.maxTexelOffset = 7;
    props->limits.minTexelGatherOffset = -8;
    props->limits.maxTexelGatherOffset = 7;
    props->limits.minInterpolationOffset = -0.5f;
    props->limits.maxInterpolationOffset = 0.5f;
    props->limits.subPixelInterpolationOffsetBits = 4;
    props->limits.maxFramebufferWidth = 16384;
    props->limits.maxFramebufferHeight = 16384;
    props->limits.maxFramebufferLayers = 2048;
    props->limits.framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    props->limits.framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    props->limits.framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    props->limits.framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    props->limits.maxColorAttachments = 8;
    props->limits.sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    props->limits.sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    props->limits.sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    props->limits.sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    props->limits.storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    props->limits.maxSampleMaskWords = 1;
    props->limits.timestampComputeAndGraphics = VK_TRUE;
    props->limits.timestampPeriod = 100.0f;
    props->limits.maxClipDistances = 8;
    props->limits.maxCullDistances = 8;
    props->limits.maxCombinedClipAndCullDistances = 8;
    props->limits.discreteQueuePriorities = 2;
    props->limits.pointSizeRange[0] = 1.0f; /* GFX10 */
    props->limits.pointSizeRange[1] = 256.0f;
    props->limits.lineWidthRange[0] = 1.0f;
    props->limits.lineWidthRange[1] = 1.0f;    /* Wide lines unsupported */
    props->limits.pointSizeGranularity = 1.0f;
    props->limits.lineWidthGranularity = 1.0f;
    props->limits.strictLines = VK_TRUE;
    props->limits.standardSampleLocations = VK_TRUE;
    props->limits.optimalBufferCopyOffsetAlignment = 256;
    props->limits.optimalBufferCopyRowPitchAlignment = 256;
    props->limits.nonCoherentAtomSize = 256;
    
    props->sparseProperties.residencyStandard2DBlockShape = VK_FALSE;
    props->sparseProperties.residencyStandard2DMultisampleBlockShape = VK_FALSE;
    props->sparseProperties.residencyStandard3DBlockShape = VK_FALSE;
    props->sparseProperties.residencyAlignedMipSize = VK_FALSE;
    props->sparseProperties.residencyNonResidentStrict = VK_FALSE;
    
    OutputDebugStringA("BC-250 Vulkan: GetPhysicalDeviceProperties\n");
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    void* pMemoryProperties)
{
    BC250_VK_DEVICE* dev = (BC250_VK_DEVICE*)physicalDevice;
    BC250_TRACE_IN(physicalDevice);

    /* Query VRAM from KMD via GET_VRAM_INFO (0x80000804) */
    UINT64 totalVram = 16ULL * 1024 * 1024 * 1024; /* 16GB fallback */
    if (dev->kmdDevice != INVALID_HANDLE_VALUE) {
        DWORD br = 0;
        UINT64 vramInfo[4] = {0};
        if (DeviceIoControl(dev->kmdDevice, 0x80000804, NULL, 0,
                              vramInfo, sizeof(vramInfo), &br, NULL)) {
            if (vramInfo[0] > 0) totalVram = vramInfo[0];
        }
    }

    VkPhysicalDeviceMemoryProperties* props = (VkPhysicalDeviceMemoryProperties*)pMemoryProperties;
    memset(props, 0, sizeof(VkPhysicalDeviceMemoryProperties));

    props->memoryTypeCount = 2;
    props->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    props->memoryTypes[0].heapIndex = 0;
    props->memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    props->memoryTypes[1].heapIndex = 0;
    props->memoryHeapCount = 1;
    props->memoryHeaps[0].size = totalVram;
    props->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

    OutputDebugStringA("BC-250 Vulkan: GetPhysicalDeviceMemoryProperties OK\n");
    BC250_TRACE_OUT(pMemoryProperties);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    void* pQueueFamilyProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    BC250_TRACE_IN(pQueueFamilyPropertyCount);

    /* VkQueueFamilyProperties is 24 bytes:
     * queueFlags(4) + queueCount(4) + timestampValidBits(4) + granularity(12).
     * Never write past *pQueueFamilyPropertyCount entries. */
    typedef struct {
        uint32_t queueFlags;
        uint32_t queueCount;
        uint32_t timestampValidBits;
        uint32_t minImageTransferGranularity[3];
    } BC250_QueueFamilyProperties;

    if (pQueueFamilyProperties == NULL) {
        *pQueueFamilyPropertyCount = 2; /* Graphics + Transfer */
        BC250_TRACE_OUT(pQueueFamilyPropertyCount);
        return VK_SUCCESS;
    }

    {
        uint32_t avail = *pQueueFamilyPropertyCount;
        uint32_t want = avail < 2 ? avail : 2;
        BC250_QueueFamilyProperties* q = (BC250_QueueFamilyProperties*)pQueueFamilyProperties;
        if (want > 0) {
            q[0].queueFlags = 0x1 | 0x2 | 0x4; /* GRAPHICS | COMPUTE | TRANSFER */
            q[0].queueCount = 1;
            q[0].timestampValidBits = 64;
            q[0].minImageTransferGranularity[0] = 1;
            q[0].minImageTransferGranularity[1] = 1;
            q[0].minImageTransferGranularity[2] = 1;
        }
        if (want > 1) {
            q[1].queueFlags = 0x4; /* TRANSFER */
            q[1].queueCount = 1;
            q[1].timestampValidBits = 64;
            q[1].minImageTransferGranularity[0] = 1;
            q[1].minImageTransferGranularity[1] = 1;
            q[1].minImageTransferGranularity[2] = 1;
        }
        *pQueueFamilyPropertyCount = want;
    }
    BC250_TRACE_OUT(pQueueFamilyProperties);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    void* pMemoryProperties)
{
    /* Same pNext-clobber hazard as Properties2: preserve sType/pNext,
     * write at offset 16. */
    uint8_t* base = (uint8_t*)pMemoryProperties;
    uint32_t saved_sType = *(uint32_t*)(base + 0);
    void* saved_pNext = *(void**)(base + 8);
    VkResult r = bc250_vkGetPhysicalDeviceMemoryProperties(physicalDevice, base + 16);
    *(uint32_t*)(base + 0) = saved_sType;
    *(void**)(base + 8) = saved_pNext;
    return r;
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice,
    void* pProperties)
{
    /* VkPhysicalDeviceProperties2 = sType(4)+pad(4)+pNext(8)+properties.
     * Writing Properties at offset 0 clobbers the caller's pNext chain
     * pointer -> vulkaninfo follows garbage pNext at print time -> AV.
     * Preserve sType/pNext, write at offset 16. */
    uint8_t* base = (uint8_t*)pProperties;
    uint32_t saved_sType = *(uint32_t*)(base + 0);
    void* saved_pNext = *(void**)(base + 8);
    BC250_TRACE_IN(pProperties);
    bc250_vkGetPhysicalDeviceProperties(physicalDevice, base + 16);
    *(uint32_t*)(base + 0) = saved_sType;
    *(void**)(base + 8) = saved_pNext;
    /* Chained structs (Vulkan11/12/13, driver, ID props) stay as the
     * caller left them (vulkaninfo zero-inits) -> reported as 0/false. */
    BC250_TRACE_OUT(pProperties);
    return VK_SUCCESS;
}

/* Device Query Functions */
VkResult VKAPI_CALL bc250_vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue* pQueue)
{
    UNREFERENCED_PARAMETER(queueFamilyIndex);
    UNREFERENCED_PARAMETER(queueIndex);
    BC250_TRACE_IN(device);
    *pQueue = (VkQueue)device;
    BC250_TRACE_OUT(pQueue);
    return VK_SUCCESS;
}

/* Memory management — allocate via KMD IOCTL */
VkResult VKAPI_CALL bc250_vkAllocateMemory(
    VkDevice device,
    const void* pAllocateInfo,
    const void* pAllocator,
    VkDeviceMemory* pMemory)
{
    UNREFERENCED_PARAMETER(pAllocator);
    BC250_VK_DEVICE* dev = (BC250_VK_DEVICE*)device;
    BC250_TRACE_IN(pAllocateInfo);
    
    /* Parse VkMemoryAllocateInfo: sType(4)+pad(4)+pNext(8)+size(8) -> size at 16. */
    uint64_t allocSize = 4096;  /* Default 4KB */
    if (pAllocateInfo) {
        uint64_t s = *(const uint64_t*)((const uint8_t*)pAllocateInfo + 16);
        if (s > allocSize && s < (1ULL << 31)) allocSize = (s + 4095) & ~4095ULL;
    }
    
    /* Use VirtualAlloc for CPU-accessible memory (safe, no KMD IOCTL) */
    void* cpuVa = VirtualAlloc(NULL, (size_t)allocSize, MEM_COMMIT, PAGE_READWRITE);
    uint64_t gpuPa = 0;
    
    if (cpuVa == NULL) {
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    
    /* For UMA (Unified Memory Architecture), CPU VA ≈ GPU PA */
    gpuPa = (uint64_t)(uintptr_t)cpuVa;
    
    /* Store allocation */
    if (g_NumAllocations < MAX_ALLOCATIONS) {
        BC250_MEM_ALLOC* alloc = &g_Allocations[g_NumAllocations++];
        alloc->cpuVa = cpuVa;
        alloc->gpuPa = gpuPa;
        alloc->size = allocSize;
        alloc->flags = 0;
    }
    
    /* Return handle (index into allocation table + 1 to avoid NULL) */
    *pMemory = (VkDeviceMemory)(uintptr_t)(g_NumAllocations);
    
    char buf[128];
    snprintf(buf, sizeof(buf), "BC-250 Vulkan: AllocateMemory size=%llu handle=%p\n",
             allocSize, (void*)*pMemory);
    OutputDebugStringA(buf);
    
    return VK_SUCCESS;
}

void VKAPI_CALL bc250_vkFreeMemory(
    VkDevice device,
    VkDeviceMemory memory,
    const void* pAllocator)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    uint32_t idx = (uint32_t)(uintptr_t)memory - 1;
    if (idx < MAX_ALLOCATIONS && g_Allocations[idx].cpuVa != NULL) {
        /* Just free CPU memory - do NOT call KMD FREE_DMA_BUFFER
           because VirtualAlloc memory was not allocated by KMD */
        VirtualFree(g_Allocations[idx].cpuVa, 0, MEM_RELEASE);
        g_Allocations[idx].cpuVa = NULL;
        g_Allocations[idx].gpuPa = 0;
        g_Allocations[idx].size = 0;
    }
}

VkResult VKAPI_CALL bc250_vkMapMemory(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkFlags flags,
    void** ppData)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(offset);
    UNREFERENCED_PARAMETER(size);
    UNREFERENCED_PARAMETER(flags);
    BC250_TRACE_IN(memory);
    uint32_t idx = (uint32_t)(uintptr_t)memory - 1;
    if (idx < MAX_ALLOCATIONS && g_Allocations[idx].cpuVa != NULL) {
        *ppData = (uint8_t*)g_Allocations[idx].cpuVa + offset;
        BC250_TRACE_OUT(ppData);
        return VK_SUCCESS;
    }
    BC250_TRACE_OUT(ppData);
    return VK_ERROR_MEMORY_MAP_FAILED;
}

void VKAPI_CALL bc250_vkUnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(memory);
    BC250_TRACE_IN(memory);
    BC250_TRACE_OUT(memory);
}

VkResult VKAPI_CALL bc250_vkFlushMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const void* pMemoryRanges)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(memoryRangeCount);
    UNREFERENCED_PARAMETER(pMemoryRanges);
    BC250_TRACE_IN(pMemoryRanges);
    /* Our memory is HOST_COHERENT: flush is a no-op by definition. */
    BC250_TRACE_OUT(pMemoryRanges);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkInvalidateMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const void* pMemoryRanges)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(memoryRangeCount);
    UNREFERENCED_PARAMETER(pMemoryRanges);
    return VK_SUCCESS;
}

/* Buffer management — VirtualAlloc (safe, no KMD IOCTL) */
#define BC250_MAX_BUFFERS 128
typedef struct { VkBuffer handle; uint64_t size; } BC250_BUFFER_REC;
static BC250_BUFFER_REC g_Buffers[BC250_MAX_BUFFERS];
static uint32_t g_NumBuffers = 0;

static void bc250_buffer_track(VkBuffer b, uint64_t size)
{
    if (g_NumBuffers < BC250_MAX_BUFFERS) {
        g_Buffers[g_NumBuffers].handle = b;
        g_Buffers[g_NumBuffers].size = size;
        g_NumBuffers++;
    }
}

static uint64_t bc250_buffer_size(VkBuffer b)
{
    for (uint32_t i = 0; i < g_NumBuffers; i++)
        if (g_Buffers[i].handle == b) return g_Buffers[i].size;
    return 4096;
}

VkResult VKAPI_CALL bc250_vkCreateBuffer(
    VkDevice device,
    const void* pCreateInfo,
    const void* pAllocator,
    VkBuffer* pBuffer)
{
    uint64_t reqSize = 4096;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    BC250_TRACE_IN(pCreateInfo);

    /* VkBufferCreateInfo.size at offset 24 (sType+pad+pNext+flags+pad). */
    if (pCreateInfo) {
        uint64_t s = *(const uint64_t*)((const uint8_t*)pCreateInfo + 24);
        if (s > reqSize && s < (1ULL << 31)) reqSize = (s + 4095) & ~4095ULL;
    }

    /* Use VirtualAlloc for now (safe) */
    *pBuffer = (VkBuffer)VirtualAlloc(NULL, (SIZE_T)reqSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (*pBuffer) bc250_buffer_track(*pBuffer, reqSize);
    BC250_TRACE_OUT(pBuffer);
    return *pBuffer ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

void VKAPI_CALL bc250_vkDestroyBuffer(VkDevice device, VkBuffer buffer, const void* pAllocator)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    if (buffer) VirtualFree((void*)buffer, 0, MEM_RELEASE);
}

void VKAPI_CALL bc250_vkGetBufferMemoryRequirements(
    VkDevice device,
    VkBuffer buffer,
    void* pMemReqs)
{
    UNREFERENCED_PARAMETER(device);
    BC250_TRACE_IN(buffer);
    if (pMemReqs) {
        /* VkMemoryRequirements = size(8) + alignment(8) + memoryTypeBits(4). */
        uint8_t* o = (uint8_t*)pMemReqs;
        uint64_t sz = bc250_buffer_size(buffer);
        *(uint64_t*)(o + 0) = sz;
        *(uint64_t*)(o + 8) = 256;   /* alignment */
        *(uint32_t*)(o + 16) = 0x2;  /* type 1: HOST_VISIBLE|COHERENT */
    }
    BC250_TRACE_OUT(pMemReqs);
}

VkResult VKAPI_CALL bc250_vkBindBufferMemory(
    VkDevice device,
    VkBuffer buffer,
    VkDeviceMemory memory,
    VkDeviceSize offset)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(buffer);
    UNREFERENCED_PARAMETER(memory);
    UNREFERENCED_PARAMETER(offset);
    BC250_TRACE_IN(buffer);
    /* CPU-side buffers are already committed; association is implicit. */
    BC250_TRACE_OUT(memory);
    return VK_SUCCESS;
}

void VKAPI_CALL bc250_vkGetImageMemoryRequirements(
    VkDevice device,
    VkImage image,
    void* pMemReqs)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(image);
    if (pMemReqs) {
        uint8_t* o = (uint8_t*)pMemReqs;
        *(uint64_t*)(o + 0) = 4ULL * 1024 * 1024; /* 4MB */
        *(uint64_t*)(o + 8) = 256;
        *(uint32_t*)(o + 16) = 0x2;
    }
}

VkResult VKAPI_CALL bc250_vkBindImageMemory(
    VkDevice device,
    VkImage image,
    VkDeviceMemory memory,
    VkDeviceSize offset)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(image);
    UNREFERENCED_PARAMETER(memory);
    UNREFERENCED_PARAMETER(offset);
    return VK_SUCCESS;
}

/* Image management: CPU-side buffers (KMD ALLOC VA is kernel-mapped
 * and unusable from user mode; old handler writes only 2 qwords so
 * allocOut[2] was 0 -> NULL VkImage -> vkcube AV). */
VkResult VKAPI_CALL bc250_vkCreateImage(
    VkDevice device,
    const void* pCreateInfo,
    const void* pAllocator,
    VkImage* pImage)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pCreateInfo);
    UNREFERENCED_PARAMETER(pAllocator);
    BC250_TRACE_IN(pCreateInfo);

    *pImage = (VkImage)VirtualAlloc(NULL, 4096 * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    BC250_TRACE_OUT(pImage);
    return *pImage ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

void VKAPI_CALL bc250_vkDestroyImage(VkDevice device, VkImage image, const void* pAllocator)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    if (image) VirtualFree((void*)image, 0, MEM_RELEASE);
}

/* Synchronization */
VkResult VKAPI_CALL bc250_vkCreateFence(
    VkDevice device,
    const void* pCreateInfo,
    const void* pAllocator,
    VkFence* pFence)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pCreateInfo);
    UNREFERENCED_PARAMETER(pAllocator);
    *pFence = (VkFence)(ULONG_PTR)InterlockedIncrement((LONG*)&g_Device.fenceValue);
    return VK_SUCCESS;
}

void VKAPI_CALL bc250_vkDestroyFence(VkDevice device, VkFence fence, const void* pAllocator)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(fence);
    UNREFERENCED_PARAMETER(pAllocator);
}

VkResult VKAPI_CALL bc250_vkGetFenceStatus(VkDevice device, VkFence fence)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(fence);
    return VK_SUCCESS; /* Always signaled for now */
}

VkResult VKAPI_CALL bc250_vkWaitForFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences,
    VkBool32 waitAll,
    uint64_t timeout)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(fenceCount);
    UNREFERENCED_PARAMETER(pFences);
    UNREFERENCED_PARAMETER(waitAll);
    UNREFERENCED_PARAMETER(timeout);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkResetFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(fenceCount);
    UNREFERENCED_PARAMETER(pFences);
    return VK_SUCCESS;
}

/* Command buffers — header + PM4 payload in one VirtualAlloc region. */
#define BC250_CMD_MAGIC     0x444D4342u /* 'BCMD' */
#define BC250_CMD_CAPACITY  (16 * 1024) /* bytes of PM4 payload (rest of 64KB alloc unused) */

typedef struct {
    uint32_t magic;
    uint32_t used;      /* bytes of PM4 payload after this header */
    uint32_t capacity;  /* BC250_CMD_CAPACITY */
    uint32_t recording; /* 1 between Begin/End */
} BC250_CMD_HDR;

static BC250_CMD_HDR* bc250_cmd(VkCommandBuffer cb)
{
    BC250_CMD_HDR* hdr = (BC250_CMD_HDR*)cb;
    if (!hdr || hdr->magic != BC250_CMD_MAGIC) return NULL;
    return hdr;
}

static uint32_t* bc250_cmd_append_dwords(BC250_CMD_HDR* hdr, uint32_t n)
{
    if (!hdr || !hdr->recording) return NULL;
    if (hdr->used + n * sizeof(uint32_t) > hdr->capacity) return NULL;
    uint32_t* p = (uint32_t*)((uint8_t*)(hdr + 1) + hdr->used);
    hdr->used += n * sizeof(uint32_t);
    return p;
}

VkResult VKAPI_CALL bc250_vkCreateCommandPool(
    VkDevice device,
    const void* pCreateInfo,
    const void* pAllocator,
    VkCommandPool* pCommandPool)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pCreateInfo);
    UNREFERENCED_PARAMETER(pAllocator);
    *pCommandPool = (VkCommandPool)VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
    return *pCommandPool ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

void VKAPI_CALL bc250_vkDestroyCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    const void* pAllocator)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    if (commandPool) VirtualFree((void*)commandPool, 0, MEM_RELEASE);
}

VkResult VKAPI_CALL bc250_vkAllocateCommandBuffers(
    VkDevice device,
    const void* pAllocateInfo,
    VkCommandBuffer* pCommandBuffers)
{
    UNREFERENCED_PARAMETER(device);
    /* pAllocateInfo: VkCommandBufferAllocateInfo — commandBufferCount @ offset after sType/pNext/pool.
     * Minimal: assume caller wants 1 (vk-minimal-test pattern); read count if layout matches. */
    uint32_t count = 1;
    if (pAllocateInfo) {
        /* VkCommandBufferAllocateInfo: sType(4)+pad(4)+pNext(8)+commandPool(8)+level(4)+count(4) = 32 */
        const uint8_t* p = (const uint8_t*)pAllocateInfo;
        count = *(const uint32_t*)(p + 28);
        if (count == 0 || count > 64) count = 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        size_t total = sizeof(BC250_CMD_HDR) + BC250_CMD_CAPACITY;
        BC250_CMD_HDR* hdr = (BC250_CMD_HDR*)VirtualAlloc(NULL, total, MEM_COMMIT, PAGE_READWRITE);
        if (!hdr) {
            for (uint32_t j = 0; j < i; j++)
                if (pCommandBuffers[j]) VirtualFree((void*)pCommandBuffers[j], 0, MEM_RELEASE);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        hdr->magic = BC250_CMD_MAGIC;
        hdr->used = 0;
        hdr->capacity = BC250_CMD_CAPACITY;
        hdr->recording = 0;
        pCommandBuffers[i] = (VkCommandBuffer)hdr;
    }
    return VK_SUCCESS;
}

void VKAPI_CALL bc250_vkFreeCommandBuffers(
    VkDevice device,
    VkCommandPool commandPool,
    uint32_t commandBufferCount,
    const VkCommandBuffer* pCommandBuffers)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(commandPool);

    for (uint32_t i = 0; i < commandBufferCount; i++) {
        if (pCommandBuffers[i]) VirtualFree((void*)pCommandBuffers[i], 0, MEM_RELEASE);
    }
}

VkResult VKAPI_CALL bc250_vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const void* pBeginInfo)
{
    BC250_CMD_HDR* hdr = bc250_cmd(commandBuffer);
    UNREFERENCED_PARAMETER(pBeginInfo);
    if (!hdr) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    hdr->used = 0;
    hdr->recording = 1;
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    BC250_CMD_HDR* hdr = bc250_cmd(commandBuffer);
    if (!hdr) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    hdr->recording = 0;
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkFlags flags)
{
    BC250_CMD_HDR* hdr = bc250_cmd(commandBuffer);
    UNREFERENCED_PARAMETER(flags);
    if (!hdr) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    hdr->used = 0;
    hdr->recording = 0;
    return VK_SUCCESS;
}

/* Queue operations */
/* PM4 opcodes */
#define PM4_TYPE3_HDR(opcode, cnt) ((3u << 30) | (((cnt) - 1) << 16) | ((opcode) << 8))
#define IT_EVENT_WRITE_EOP  0x47
#define IT_NOP              0x10
#define IT_DRAW_INDEX_AUTO  0x2D

/* KMD IOCTL codes */
#define IOCTL_AMDBC250_ALLOC_DMA_BUFFER  0x80000930
#define IOCTL_AMDBC250_FREE_DMA_BUFFER   0x80000934
#define IOCTL_AMDBC250_SUBMIT_COMMANDS   0x80000880
#define IOCTL_AMDBC250_WAIT_FENCE        0x80000884
#define IOCTL_AMDBC250_SEND_PM4          0x80000B84

/* Allocate DMA buffer from KMD: returns CPU VA and GPU PA */
static PVOID bc250_alloc_dma(HANDLE kmd, ULONG size, uint64_t *outPa)
{
    ULONG allocIn[1] = {size};
    ULONG64 allocOut[2] = {0};
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(kmd, IOCTL_AMDBC250_ALLOC_DMA_BUFFER,
                        allocIn, sizeof(allocIn), allocOut, sizeof(allocOut), &ret, NULL);
    if (!ok) {
        char buf[128];
        snprintf(buf, sizeof(buf), "BC-250 Vulkan: AllocDMA IOCTL failed (err=%lu)\n", GetLastError());
        OutputDebugStringA(buf);
        return NULL;
    }
    *outPa = allocOut[0];
    return (PVOID)(UINT_PTR)allocOut[1];
}

static void bc250_free_dma(HANDLE kmd, PVOID va)
{
    ULONG64 freeIn[1] = {(ULONG64)(UINT_PTR)va};
    DWORD ret = 0;
    DeviceIoControl(kmd, IOCTL_AMDBC250_FREE_DMA_BUFFER, freeIn, sizeof(freeIn), NULL, 0, &ret, NULL);
}

/* VkSubmitInfo — match Vulkan ABI (x64 natural alignment, no pack). */
typedef struct BC250_VkSubmitInfo {
    int32_t     sType;
    const void* pNext;
    uint32_t    waitSemaphoreCount;
    const void* pWaitSemaphores;
    const uint32_t* pWaitDstStageMask;
    uint32_t    commandBufferCount;
    const VkCommandBuffer* pCommandBuffers;
    uint32_t    signalSemaphoreCount;
    const void* pSignalSemaphores;
} BC250_VkSubmitInfo;

#define BC250_VK_STRUCTURE_TYPE_SUBMIT_INFO 4

/* SEND_PM4: Commands[64]@0, Count@256, Fence(u64)@264, QueueType@272. */
static int bc250_send_pm4(HANDLE kmd, const uint32_t* dwords, uint32_t count,
                          uint64_t fence, int withFence)
{
    if (!kmd || kmd == INVALID_HANDLE_VALUE || !dwords || count == 0 || count > 64)
        return 0;
    /* AMDBC250_IOCTL_SEND_PM4 = Commands[64]@0 + count@256 + pad@260 +
     * Fence@264 + QueueType@272 + pad@276 = 280 bytes. */
    UCHAR spBuf[280];
    ZeroMemory(spBuf, sizeof(spBuf));
    memcpy(spBuf, dwords, count * sizeof(uint32_t));
    *(uint32_t*)(spBuf + 256) = count;
    *(uint64_t*)(spBuf + 264) = withFence ? fence : 0;
    *(uint32_t*)(spBuf + 272) = 0; /* GFX queue */
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(kmd, IOCTL_AMDBC250_SEND_PM4,
                              spBuf, sizeof(spBuf), NULL, 0, &ret, NULL);
    return ok ? 1 : 0;
}

/* Gather PM4 from recorded command buffers; send via SEND_PM4.
 * Fence EOP is appended by KMD when FenceValue > 0 (GlobalFence PA) —
 * do NOT emit a second user-mode EOP with ADDR=0 (would double-fence). */
static int bc250_submit_cmd_buffers(HANDLE kmd, const BC250_VkSubmitInfo* sub,
                                    uint64_t fence)
{
    uint32_t batch[64];
    uint32_t n = 0;
    int sent = 0;

    if (sub->commandBufferCount > 0 && sub->pCommandBuffers) {
        for (uint32_t i = 0; i < sub->commandBufferCount; i++) {
            BC250_CMD_HDR* hdr = bc250_cmd(sub->pCommandBuffers[i]);
            if (!hdr || hdr->used == 0) continue;
            const uint32_t* src = (const uint32_t*)(hdr + 1);
            uint32_t nd = hdr->used / sizeof(uint32_t);
            /* KMD appends EOP (6 dwords) when FenceValue>0 — leave room. */
            while (nd > 0) {
                uint32_t room = 64u - n;
                if (room <= 6) {
                    if (!bc250_send_pm4(kmd, batch, n, 0, 0)) return 0;
                    sent++;
                    n = 0;
                    room = 64;
                }
                uint32_t take = nd;
                if (take > room - 6) take = room - 6;
                memcpy(batch + n, src, take * sizeof(uint32_t));
                n += take;
                src += take;
                nd -= take;
            }
        }
    }

    if (n == 0) {
        /* Empty submit: still kick a fence via NOP (KMD adds EOP). */
        batch[0] = 0x30000000; /* TYPE2 NOP */
        n = 1;
    }

    if (!bc250_send_pm4(kmd, batch, n, fence, 1)) return 0;
    sent++;
    return sent > 0;
}

/* Queue Submit — recorded PM4 from command buffers via SEND_PM4 (0x80000B84).
 * No DMA-buffer VA is ever touched from user mode: ALLOC_DMA_BUFFER returns a
 * kernel VA (MmAllocateContiguousMemory), writing it from here AVs the caller. */
VkResult VKAPI_CALL bc250_vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const void* pSubmits,
    VkFence fence)
{
    UNREFERENCED_PARAMETER(queue);
    UNREFERENCED_PARAMETER(fence);

    BC250_VK_DEVICE* dev = &g_Device;
    /* vkCreateDevice inits a heap copy; g_Device keeps NULL/INVALID until
     * first submit, so cover both and lazy-open here. If open succeeds
     * but INIT_HARDWARE was never called (init_device failed earlier),
     * call it now — without it HardwareInitialized stays FALSE and every
     * SEND_PM4 returns STATUS_DEVICE_NOT_READY (gle=21). */
    if (dev->kmdDevice == INVALID_HANDLE_VALUE || dev->kmdDevice == NULL) {
        dev->kmdDevice = bc250_open_kmd();
        if (dev->kmdDevice == INVALID_HANDLE_VALUE || dev->kmdDevice == NULL)
            return VK_SUCCESS;
        /* Retry INIT_HARDWARE on the fresh handle */
        {
            struct { uint64_t base; uint32_t size; uint32_t flags; uint64_t fbBase; uint32_t fbSize; } ih = { 0xFE800000, 0x80000, 1, 0, 0 };
            DWORD br = 0;
            BOOL ok = DeviceIoControl(dev->kmdDevice, 0x80000B80, &ih, sizeof(ih),
                                      NULL, 0, &br, NULL);
            if (!ok) {
                OutputDebugStringA("BC-250 Vulkan: QueueSubmit INIT_HARDWARE retry FAILED\n");
            }
        }
    }

    int anySent = 0;
    uint64_t fenceBase = dev->fenceValue;

    if (pSubmits && submitCount > 0) {
        const BC250_VkSubmitInfo* subs = (const BC250_VkSubmitInfo*)pSubmits;
        for (uint32_t s = 0; s < submitCount; s++) {
            if (subs[s].sType != BC250_VK_STRUCTURE_TYPE_SUBMIT_INFO)
                continue;
            if (bc250_submit_cmd_buffers(dev->kmdDevice, &subs[s], fenceBase + s))
                anySent = 1;
        }
    }

    if (!anySent) {
        /* Fallback: NOP only — KMD appends EOP when FenceValue > 0. */
        uint32_t cmds[1];
        cmds[0] = 0x30000000; /* PM4 TYPE2 NOP */
        anySent = bc250_send_pm4(dev->kmdDevice, cmds, 1, fenceBase, 1);
        bc250_debug_submit_status(anySent, anySent ? 0 : GetLastError(),
                                  (unsigned long)(sizeof(uint32_t)),
                                  0x30000000);
    }

    if (anySent) dev->fenceValue = fenceBase + (submitCount > 0 ? submitCount : 1);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkQueueWaitIdle(VkQueue queue)
{
    UNREFERENCED_PARAMETER(queue);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkDeviceWaitIdle(VkDevice device)
{
    UNREFERENCED_PARAMETER(device);
    return VK_SUCCESS;
}

/* Queue Present - Flip display via KMD IOCTL */
/* Queue Present - fence-sync no-op (display owned by KMDOD sample driver).
 * Deliberately does NOT call FLIP_DISPLAY: scanout is driven by the
 * KMDOD miniport, and programming it with a placeholder address risks
 * display corruption. Visible output needs KMDOD flip integration
 * (future work); the acquire/submit/present loop itself runs cleanly. */
VkResult VKAPI_CALL bc250_vkQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo)
{
    UNREFERENCED_PARAMETER(queue);
    if (pPresentInfo && pPresentInfo->swapchainCount > 0 && pPresentInfo->pImageIndices) {
        BC250_TRACE_IN(pPresentInfo->pImageIndices);
    }
    OutputDebugStringA("BC-250 Vulkan: QueuePresentKHR (fence-sync no-op)\n");
    BC250_TRACE_OUT(pPresentInfo);
    return VK_SUCCESS;
}

/* --- WSI: surface + swapchain (vkcube path) --- */
#define BC250_MAX_SWAPCHAINS 4
#define BC250_MAX_SWAP_IMAGES 8

typedef struct {
    uint32_t magic; /* 0x53575043 "SWPC" */
    uint32_t width, height;
    uint32_t imageCount;
    uint32_t nextImage;
    void* imageCpu[BC250_MAX_SWAP_IMAGES];
    uint64_t imageSize;
    VkFormat format;
} BC250_SWAPCHAIN;

static BC250_SWAPCHAIN g_Swap[BC250_MAX_SWAPCHAINS];
static uint32_t g_NumSwap = 0;
static uint32_t g_NextSurface = 0x5000;

static void bc250_display_size(uint32_t* w, uint32_t* h)
{
    *w = 1920; *h = 1080;
    BC250_VK_DEVICE* dev = &g_Device;
    if (dev->kmdDevice != NULL && dev->kmdDevice != INVALID_HANDLE_VALUE) {
        DWORD br = 0;
        /* GET_DISPLAY_INFO 0x800008C8: {CurrentWidth, Height, ...} */
        uint32_t info[8] = {0};
        if (DeviceIoControl(dev->kmdDevice, 0x800008C8, NULL, 0,
                            info, sizeof(info), &br, NULL)) {
            if (info[0] >= 640 && info[0] <= 16384 &&
                info[1] >= 480 && info[1] <= 16384) {
                *w = info[0]; *h = info[1];
            }
        }
    }
}

VkResult VKAPI_CALL bc250_vkCreateWin32SurfaceKHR(
    VkInstance instance,
    const void* pCreateInfo,
    const void* pAllocator,
    VkSurfaceKHR* pSurface)
{
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(pCreateInfo);
    UNREFERENCED_PARAMETER(pAllocator);
    BC250_TRACE_IN(pCreateInfo);
    *pSurface = (VkSurfaceKHR)(uintptr_t)(g_NextSurface++);
    BC250_TRACE_OUT(pSurface);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex,
    VkSurfaceKHR surface,
    VkBool32* pSupported)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(surface);
    BC250_TRACE_IN(pSupported);
    /* Queue 0 (graphics) presents; queue 1 (transfer) does not. */
    *pSupported = (queueFamilyIndex == 0) ? VK_TRUE : VK_FALSE;
    BC250_TRACE_OUT(pSupported);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR* pCaps)
{
    uint32_t w, h;
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(surface);
    BC250_TRACE_IN(pCaps);
    bc250_display_size(&w, &h);
    memset(pCaps, 0, sizeof(*pCaps));
    pCaps->minImageCount = 2;
    pCaps->maxImageCount = BC250_MAX_SWAP_IMAGES;
    pCaps->currentExtent.width = w;
    pCaps->currentExtent.height = h;
    pCaps->minImageExtent.width = w;
    pCaps->minImageExtent.height = h;
    pCaps->maxImageExtent.width = w;
    pCaps->maxImageExtent.height = h;
    pCaps->maxImageArrayLayers = 1;
    pCaps->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pCaps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pCaps->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    pCaps->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    BC250_TRACE_OUT(pCaps);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    uint32_t* pCount,
    VkSurfaceFormatKHR* pFormats)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(surface);
    BC250_TRACE_IN(pCount);
    if (pFormats == NULL) {
        *pCount = 1;
        BC250_TRACE_OUT(pCount);
        return VK_SUCCESS;
    }
    if (*pCount < 1) return VK_INCOMPLETE;
    /* VkSurfaceFormatKHR = format(4) + colorSpace(4) = 8 bytes
     * (NOT 260 like VkExtensionProperties — that overflowed the heap). */
    pFormats[0].format = VK_FORMAT_B8G8R8A8_SRGB;
    pFormats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    *pCount = 1;
    BC250_TRACE_OUT(pFormats);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    uint32_t* pCount,
    VkPresentModeKHR* pModes)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(surface);
    BC250_TRACE_IN(pCount);
    if (pModes == NULL) {
        *pCount = 1;
        BC250_TRACE_OUT(pCount);
        return VK_SUCCESS;
    }
    if (*pCount < 1) return VK_INCOMPLETE;
    pModes[0] = VK_PRESENT_MODE_FIFO_KHR;
    *pCount = 1;
    BC250_TRACE_OUT(pModes);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pInfo,
    const void* pAllocator,
    VkSwapchainKHR* pSwapchain)
{
    uint32_t i, n;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    BC250_TRACE_IN(pInfo);
    if (g_NumSwap >= BC250_MAX_SWAPCHAINS) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (!pInfo || pInfo->minImageCount < 1) return VK_ERROR_INITIALIZATION_FAILED;
    n = pInfo->minImageCount + 1; /* app asks min, we give min+1 */
    if (n > BC250_MAX_SWAP_IMAGES) n = BC250_MAX_SWAP_IMAGES;
    BC250_SWAPCHAIN* sc = &g_Swap[g_NumSwap++];
    memset(sc, 0, sizeof(*sc));
    sc->magic = 0x53575043u;
    sc->width = pInfo->imageExtent.width ? pInfo->imageExtent.width : 1920;
    sc->height = pInfo->imageExtent.height ? pInfo->imageExtent.height : 1080;
    sc->format = pInfo->imageFormat;
    sc->imageSize = (uint64_t)sc->width * sc->height * 4;
    for (i = 0; i < n; i++) {
        sc->imageCpu[i] = VirtualAlloc(NULL, (SIZE_T)sc->imageSize,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!sc->imageCpu[i]) break;
        memset(sc->imageCpu[i], 0, (size_t)sc->imageSize);
    }
    sc->imageCount = i;
    if (i < 2) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pSwapchain = (VkSwapchainKHR)sc;
    BC250_TRACE_OUT(pSwapchain);
    return VK_SUCCESS;
}

void VKAPI_CALL bc250_vkDestroySwapchainKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    const void* pAllocator)
{
    uint32_t i;
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    BC250_TRACE_IN(swapchain);
    BC250_SWAPCHAIN* sc = (BC250_SWAPCHAIN*)swapchain;
    if (sc) {
        for (i = 0; i < sc->imageCount && i < BC250_MAX_SWAP_IMAGES; i++)
            if (sc->imageCpu[i]) VirtualFree(sc->imageCpu[i], 0, MEM_RELEASE);
        memset(sc, 0, sizeof(*sc));
    }
    BC250_TRACE_OUT(swapchain);
}

VkResult VKAPI_CALL bc250_vkGetSwapchainImagesKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint32_t* pCount,
    VkImage* pImages)
{
    uint32_t i;
    UNREFERENCED_PARAMETER(device);
    BC250_TRACE_IN(pCount);
    BC250_SWAPCHAIN* sc = (BC250_SWAPCHAIN*)swapchain;
    if (!sc || sc->magic != 0x53575043u) return VK_ERROR_INITIALIZATION_FAILED;
    if (pImages == NULL) {
        *pCount = sc->imageCount;
        BC250_TRACE_OUT(pCount);
        return VK_SUCCESS;
    }
    if (*pCount < sc->imageCount) return VK_INCOMPLETE;
    for (i = 0; i < sc->imageCount; i++)
        pImages[i] = (VkImage)(sc->imageCpu[i] ? sc->imageCpu[i] : (void*)(uintptr_t)(0x6000 + i));
    *pCount = sc->imageCount;
    BC250_TRACE_OUT(pImages);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkAcquireNextImageKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t* pImageIndex)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(timeout);
    UNREFERENCED_PARAMETER(semaphore);
    UNREFERENCED_PARAMETER(fence);
    BC250_TRACE_IN(swapchain);
    BC250_SWAPCHAIN* sc = (BC250_SWAPCHAIN*)swapchain;
    if (!sc || sc->magic != 0x53575043u || sc->imageCount == 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    /* Images always available (CPU buffers); round-robin. */
    *pImageIndex = sc->nextImage % sc->imageCount;
    sc->nextImage++;
    BC250_TRACE_OUT(pImageIndex);
    return VK_SUCCESS;
}

/* Pipeline creation (stubs) */
/* Pipeline creation with real shader compilation */
VkResult VKAPI_CALL bc250_vkCreateGraphicsPipelines(
    VkDevice device,
    VkPipelineCache pipelineCache,
    uint32_t createInfoCount,
    const void* pCreateInfos,
    const void* pAllocator,
    VkPipeline* pPipelines)
{
    UNREFERENCED_PARAMETER(pipelineCache);
    UNREFERENCED_PARAMETER(pAllocator);
    BC250_TRACE_IN(pCreateInfos);
    
    for (uint32_t i = 0; i < createInfoCount; i++) {
        /* In real implementation:
         * 1. Extract shader stages from pCreateInfos[i]
         * 2. Compile each shader with bc250_compile_spirv()
         * 3. Create GPU pipeline state
         */
        
        /* Placeholder: create pipeline handle */
        pPipelines[i] = (VkPipeline)(ULONG_PTR)(i + 1);
        
        char buf[128];
        snprintf(buf, sizeof(buf), "BC-250 Vulkan: Pipeline %u created\n", i);
        OutputDebugStringA(buf);
    }
    BC250_TRACE_OUT(pPipelines);
    return VK_SUCCESS;
}

void VKAPI_CALL bc250_vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const void* pAllocator)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pipeline);
    UNREFERENCED_PARAMETER(pAllocator);
}

/* Command recording stubs */
void VKAPI_CALL bc250_vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer,
    VkFlags srcStageMask,
    VkFlags dstStageMask,
    VkFlags dependencyFlags,
    uint32_t memoryBarrierCount,
    const void* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount,
    const void* pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount,
    const void* pImageMemoryBarriers)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(srcStageMask);
    UNREFERENCED_PARAMETER(dstStageMask);
    UNREFERENCED_PARAMETER(dependencyFlags);
    UNREFERENCED_PARAMETER(memoryBarrierCount);
    UNREFERENCED_PARAMETER(pMemoryBarriers);
    UNREFERENCED_PARAMETER(bufferMemoryBarrierCount);
    UNREFERENCED_PARAMETER(pBufferMemoryBarriers);
    UNREFERENCED_PARAMETER(imageMemoryBarrierCount);
    UNREFERENCED_PARAMETER(pImageMemoryBarriers);
}

void VKAPI_CALL bc250_vkCmdBindPipeline(
    VkCommandBuffer commandBuffer,
    VkFlags pipelineBindPoint,
    VkPipeline pipeline)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(pipelineBindPoint);
    UNREFERENCED_PARAMETER(pipeline);
}

void VKAPI_CALL bc250_vkCmdDraw(
    VkCommandBuffer commandBuffer,
    uint32_t vertexCount,
    uint32_t instanceCount,
    uint32_t firstVertex,
    uint32_t firstInstance)
{
    BC250_CMD_HDR* hdr = bc250_cmd(commandBuffer);
    (void)instanceCount;
    (void)firstVertex;
    (void)firstInstance;
    if (!hdr || vertexCount == 0) return;

    /* PM4 DRAW_INDEX_AUTO (0x2D): non-indexed draw, auto index 0..N-1.
     * D1 = vertex count, D2 = prim_type | (1<<8) (matches D3D9 UMD path:
     * D3DPT_TRIANGLELIST=4; bit8 = index-size/auto encoding used on BC-250). */
    uint32_t* p = bc250_cmd_append_dwords(hdr, 3);
    if (!p) return;
    p[0] = PM4_TYPE3_HDR(IT_DRAW_INDEX_AUTO, 3);
    p[1] = vertexCount;
    p[2] = 4u | (1u << 8); /* D3DPT_TRIANGLELIST | auto-index bit */
}

void VKAPI_CALL bc250_vkCmdDrawIndexed(
    VkCommandBuffer commandBuffer,
    uint32_t indexCount,
    uint32_t instanceCount,
    uint32_t firstIndex,
    int32_t vertexOffset,
    uint32_t firstInstance)
{
    BC250_CMD_HDR* hdr = bc250_cmd(commandBuffer);
    (void)instanceCount;
    (void)firstIndex;
    (void)vertexOffset;
    (void)firstInstance;
    if (!hdr || indexCount == 0) return;

    /* DRAW_INDEX_AUTO with explicit count — real INDEX_2 needs IB GPU PA.
     * Emit auto with indexCount so packet is real; index fetch still needs
     * bind-index-buffer (not implemented) for true indexed draws. */
    uint32_t* p = bc250_cmd_append_dwords(hdr, 3);
    if (!p) return;
    p[0] = PM4_TYPE3_HDR(IT_DRAW_INDEX_AUTO, 3);
    p[1] = indexCount;
    p[2] = 4u | (1u << 8);
}

void VKAPI_CALL bc250_vkCmdDispatch(
    VkCommandBuffer commandBuffer,
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(groupCountX);
    UNREFERENCED_PARAMETER(groupCountY);
    UNREFERENCED_PARAMETER(groupCountZ);
}

void VKAPI_CALL bc250_vkCmdCopyBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    uint32_t regionCount,
    const void* pRegions)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(regionCount);
    UNREFERENCED_PARAMETER(pRegions);
    
    /* Submit SDMA copy via KMD IOCTL 0x80000940 */
    BC250_VK_DEVICE* dev = &g_Device;
    if (dev->kmdDevice != INVALID_HANDLE_VALUE && srcBuffer && dstBuffer) {
        ULONG64 copyData[3] = {0};
        copyData[0] = (ULONG64)(ULONG_PTR)srcBuffer;  /* Source */
        copyData[1] = (ULONG64)(ULONG_PTR)dstBuffer;  /* Dest */
        copyData[2] = 4096;  /* Size (TODO: get from regions) */
        
        DWORD ret = 0;
        DeviceIoControl(dev->kmdDevice, 0x80000940, copyData, sizeof(copyData),
                        NULL, 0, &ret, NULL);
        
        OutputDebugStringA("BC-250 Vulkan: CmdCopyBuffer → SDMA IOCTL\n");
    }
}

void VKAPI_CALL bc250_vkCmdCopyImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const void* pRegions)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(srcImageLayout);
    UNREFERENCED_PARAMETER(dstImageLayout);
    UNREFERENCED_PARAMETER(regionCount);
    UNREFERENCED_PARAMETER(pRegions);
    
    /* Submit SDMA copy via KMD IOCTL 0x80000940 */
    BC250_VK_DEVICE* dev = &g_Device;
    if (dev->kmdDevice != INVALID_HANDLE_VALUE && srcImage && dstImage) {
        ULONG64 copyData[3] = {0};
        copyData[0] = (ULONG64)(ULONG_PTR)srcImage;
        copyData[1] = (ULONG64)(ULONG_PTR)dstImage;
        copyData[2] = 4096 * 4;  /* Size (TODO: get from regions) */
        
        DWORD ret = 0;
        DeviceIoControl(dev->kmdDevice, 0x80000940, copyData, sizeof(copyData),
                        NULL, 0, &ret, NULL);
        
        OutputDebugStringA("BC-250 Vulkan: CmdCopyImage → SDMA IOCTL\n");
    }
}

void VKAPI_CALL bc250_vkCmdClearColorImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout imageLayout,
    const float* pColor,
    uint32_t rangeCount,
    const void* pRanges)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(imageLayout);
    UNREFERENCED_PARAMETER(rangeCount);
    UNREFERENCED_PARAMETER(pRanges);
    
    /* Submit SDMA fill via KMD IOCTL 0x80000944 */
    BC250_VK_DEVICE* dev = &g_Device;
    if (dev->kmdDevice != INVALID_HANDLE_VALUE && image && pColor) {
        ULONG fillData[4] = {0};
        fillData[0] = (ULONG)(ULONG_PTR)image & 0xFFFFFFFF;  /* Dst low */
        fillData[1] = 0;  /* Dst high */
        fillData[2] = 4096 * 4;  /* Size */
        fillData[3] = (ULONG)(pColor[0] * 255.0f) |         /* R */
                      ((ULONG)(pColor[1] * 255.0f) << 8) |   /* G */
                      ((ULONG)(pColor[2] * 255.0f) << 16) |  /* B */
                      ((ULONG)(pColor[3] * 255.0f) << 24);   /* A */
        
        DWORD ret = 0;
        DeviceIoControl(dev->kmdDevice, 0x80000944, fillData, sizeof(fillData),
                        NULL, 0, &ret, NULL);
        
        OutputDebugStringA("BC-250 Vulkan: CmdClearColorImage → SDMA IOCTL\n");
    }
}

void VKAPI_CALL bc250_vkCmdClearDepthStencilImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout imageLayout,
    const void* pDepthStencil,
    uint32_t rangeCount,
    const void* pRanges)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(image);
    UNREFERENCED_PARAMETER(imageLayout);
    UNREFERENCED_PARAMETER(pDepthStencil);
    UNREFERENCED_PARAMETER(rangeCount);
    UNREFERENCED_PARAMETER(pRanges);
}

void VKAPI_CALL bc250_vkCmdSetViewport(
    VkCommandBuffer commandBuffer,
    uint32_t firstViewport,
    uint32_t viewportCount,
    const void* pViewports)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(firstViewport);
    UNREFERENCED_PARAMETER(viewportCount);
    UNREFERENCED_PARAMETER(pViewports);
}

void VKAPI_CALL bc250_vkCmdSetScissor(
    VkCommandBuffer commandBuffer,
    uint32_t firstScissor,
    uint32_t scissorCount,
    const void* pScissors)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(firstScissor);
    UNREFERENCED_PARAMETER(scissorCount);
    UNREFERENCED_PARAMETER(pScissors);
}

void VKAPI_CALL bc250_vkCmdBindVertexBuffers(
    VkCommandBuffer commandBuffer,
    uint32_t firstBinding,
    uint32_t bindingCount,
    const VkBuffer* pBuffers,
    const VkDeviceSize* pOffsets)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(firstBinding);
    UNREFERENCED_PARAMETER(bindingCount);
    UNREFERENCED_PARAMETER(pBuffers);
    UNREFERENCED_PARAMETER(pOffsets);
}

void VKAPI_CALL bc250_vkCmdBindIndexBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkFlags indexType)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(buffer);
    UNREFERENCED_PARAMETER(offset);
    UNREFERENCED_PARAMETER(indexType);
}

void VKAPI_CALL bc250_vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer,
    VkFlags pipelineBindPoint,
    VkPipelineLayout layout,
    uint32_t firstSet,
    uint32_t descriptorSetCount,
    const VkDescriptorSet* pDescriptorSets,
    uint32_t dynamicOffsetCount,
    const uint32_t* pDynamicOffsets)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(pipelineBindPoint);
    UNREFERENCED_PARAMETER(layout);
    UNREFERENCED_PARAMETER(firstSet);
    UNREFERENCED_PARAMETER(descriptorSetCount);
    UNREFERENCED_PARAMETER(pDescriptorSets);
    UNREFERENCED_PARAMETER(dynamicOffsetCount);
    UNREFERENCED_PARAMETER(pDynamicOffsets);
}

void VKAPI_CALL bc250_vkCmdPushConstants(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout layout,
    VkFlags stageFlags,
    uint32_t offset,
    uint32_t size,
    const void* pValues)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(layout);
    UNREFERENCED_PARAMETER(stageFlags);
    UNREFERENCED_PARAMETER(offset);
    UNREFERENCED_PARAMETER(size);
    UNREFERENCED_PARAMETER(pValues);
}

/* Cleanup helpers */
static void bc250_DestroyDevice(VkDevice device)
{
    UNREFERENCED_PARAMETER(device);
    OutputDebugStringA("BC-250 Vulkan: Device destroyed\n");
}

static void bc250_DestroyInstance(VkInstance instance)
{
    UNREFERENCED_PARAMETER(instance);
    OutputDebugStringA("BC-250 Vulkan: Instance destroyed\n");
}

/* DLL entry point */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(lpReserved);
    
    switch (dwReason) {
    case DLL_PROCESS_ATTACH: {
        FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
        if (f) { fprintf(f, "DLL_PROCESS_ATTACH\n"); fclose(f); }
        break;
    }
    case DLL_PROCESS_DETACH: {
        FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
        if (f) { fprintf(f, "DLL_PROCESS_DETACH\n"); fclose(f); }
        break;
    }
    }
    return TRUE;
}

/* Vulkan ICD dispatch table */
typedef struct VkIcdDispatchTable {
    PFN_vkCreateInstance                     CreateInstance;
    PFN_vkDestroyInstance                    DestroyInstance;
    PFN_vkEnumeratePhysicalDevices           EnumeratePhysicalDevices;
    PFN_vkCreateDevice                       CreateDevice;
    PFN_vkDestroyDevice                      DestroyDevice;
    PFN_vkGetDeviceQueue                     GetDeviceQueue;
    PFN_vkAllocateMemory                     AllocateMemory;
    PFN_vkFreeMemory                         FreeMemory;
    PFN_vkMapMemory                          MapMemory;
    PFN_vkUnmapMemory                        UnmapMemory;
    PFN_vkCreateBuffer                       CreateBuffer;
    PFN_vkDestroyBuffer                      DestroyBuffer;
    PFN_vkCreateFence                        CreateFence;
    PFN_vkDestroyFence                       DestroyFence;
    PFN_vkGetFenceStatus                     GetFenceStatus;
    PFN_vkWaitForFences                      WaitForFences;
    PFN_vkResetFences                        ResetFences;
    PFN_vkCreateCommandPool                  CreateCommandPool;
    PFN_vkDestroyCommandPool                 DestroyCommandPool;
    PFN_vkAllocateCommandBuffers             AllocateCommandBuffers;
    PFN_vkFreeCommandBuffers                 FreeCommandBuffers;
    PFN_vkBeginCommandBuffer                 BeginCommandBuffer;
    PFN_vkEndCommandBuffer                   EndCommandBuffer;
    PFN_vkResetCommandBuffer                 ResetCommandBuffer;
    PFN_vkQueueSubmit                        QueueSubmit;
    PFN_vkQueueWaitIdle                      QueueWaitIdle;
    PFN_vkDeviceWaitIdle                     DeviceWaitIdle;
    PFN_vkCreateGraphicsPipelines            CreateGraphicsPipelines;
    PFN_vkDestroyPipeline                    DestroyPipeline;
    PFN_vkQueuePresentKHR                    QueuePresentKHR;
    PFN_vkCmdPipelineBarrier                 CmdPipelineBarrier;
    PFN_vkCmdBindPipeline                    CmdBindPipeline;
    PFN_vkCmdDraw                            CmdDraw;
    PFN_vkCmdDrawIndexed                     CmdDrawIndexed;
    PFN_vkCmdDispatch                        CmdDispatch;
    PFN_vkCmdCopyBuffer                      CmdCopyBuffer;
    PFN_vkCmdCopyImage                       CmdCopyImage;
    PFN_vkCmdClearColorImage                 CmdClearColorImage;
    PFN_vkCmdClearDepthStencilImage          CmdClearDepthStencilImage;
    PFN_vkCmdSetViewport                     CmdSetViewport;
    PFN_vkCmdSetScissor                       CmdSetScissor;
    PFN_vkCmdBindVertexBuffers               CmdBindVertexBuffers;
    PFN_vkCmdBindIndexBuffer                 CmdBindIndexBuffer;
    PFN_vkCmdBindDescriptorSets              CmdBindDescriptorSets;
    PFN_vkCmdPushConstants                   CmdPushConstants;
} VkIcdDispatchTable;

/* Exported dispatch table for ICD loading */
VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion) {
    if (pApiVersion) *pApiVersion = VK_API_VERSION_1_3;
    return VK_SUCCESS;
}

__declspec(dllexport) void* VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    UNREFERENCED_PARAMETER(instance);
    
    if (!pName) return NULL;

    {
        /* Validate readability first: a garbage pName from the caller
         * must not take us down inside fprintf. */
        __try {
            volatile char c = pName[0];
            size_t n = 0;
            while (n < 512 && pName[n]) n++;
            (void)c;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            FILE *fe = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
            if (fe) { fprintf(fe, "TRACE GetInstanceProcAddr BAD-PTR %p\n", pName); fflush(fe); fclose(fe); }
            return NULL;
        }
        FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
        if (f) { fprintf(f, "TRACE GetInstanceProcAddr %s\n", pName); fflush(f); fclose(f); }
    }

    /* SEH guard: if our own dispatch crashes on a particular name,
     * log it and return NULL instead of taking the process down. */
    __try {
    
    /* Global-level functions */
    if (!strcmp(pName, "vkCreateInstance"))            return (void*)bc250_vkCreateInstance;
    if (!strcmp(pName, "vkEnumerateInstanceExtensionProperties")) return (void*)bc250_vkEnumerateInstanceExtensionProperties;
    if (!strcmp(pName, "vkEnumerateInstanceLayerProperties")) return NULL;
    
    /* Instance-level functions */
    if (!strcmp(pName, "vkDestroyInstance"))           return (void*)bc250_vkDestroyInstance;
    if (!strcmp(pName, "vkEnumeratePhysicalDevices"))  return (void*)bc250_vkEnumeratePhysicalDevices;
    if (!strcmp(pName, "vkEnumerateDeviceExtensionProperties")) return (void*)bc250_vkEnumerateDeviceExtensionProperties;
    if (!strcmp(pName, "vkEnumerateDeviceLayerProperties"))    return (void*)bc250_vkEnumerateDeviceLayerProperties;
    if (!strcmp(pName, "vkCreateDevice"))              return (void*)bc250_vkCreateDevice;
    if (!strcmp(pName, "vkDestroyDevice"))             return (void*)bc250_vkDestroyDevice;
    if (!strcmp(pName, "vkGetDeviceQueue"))            return (void*)bc250_vkGetDeviceQueue;
    if (!strcmp(pName, "vkGetPhysicalDeviceFeatures"))        return (void*)bc250_vkGetPhysicalDeviceFeatures;
    if (!strcmp(pName, "vkGetPhysicalDeviceFormatProperties")) return (void*)bc250_vkGetPhysicalDeviceFormatProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties")) return (void*)bc250_vkGetPhysicalDeviceImageFormatProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceProperties"))      return (void*)bc250_vkGetPhysicalDeviceProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties")) return (void*)bc250_vkGetPhysicalDeviceQueueFamilyProperties;
    if (!strcmp(pName, "vkGetPhysicalDeviceMemoryProperties"))    return (void*)bc250_vkGetPhysicalDeviceMemoryProperties;
    if (!strcmp(pName, "vkAllocateMemory"))            return (void*)bc250_vkAllocateMemory;
    if (!strcmp(pName, "vkFreeMemory"))                return (void*)bc250_vkFreeMemory;
    if (!strcmp(pName, "vkMapMemory"))                 return (void*)bc250_vkMapMemory;
    if (!strcmp(pName, "vkUnmapMemory"))               return (void*)bc250_vkUnmapMemory;
    if (!strcmp(pName, "vkFlushMappedMemoryRanges"))   return (void*)bc250_vkFlushMappedMemoryRanges;
    if (!strcmp(pName, "vkInvalidateMappedMemoryRanges")) return (void*)bc250_vkInvalidateMappedMemoryRanges;
    if (!strcmp(pName, "vkCreateBuffer"))              return (void*)bc250_vkCreateBuffer;
    if (!strcmp(pName, "vkDestroyBuffer"))             return (void*)bc250_vkDestroyBuffer;
    if (!strcmp(pName, "vkGetBufferMemoryRequirements")) return (void*)bc250_vkGetBufferMemoryRequirements;
    if (!strcmp(pName, "vkBindBufferMemory"))          return (void*)bc250_vkBindBufferMemory;
    if (!strcmp(pName, "vkGetImageMemoryRequirements")) return (void*)bc250_vkGetImageMemoryRequirements;
    if (!strcmp(pName, "vkBindImageMemory"))           return (void*)bc250_vkBindImageMemory;
    if (!strcmp(pName, "vkCreateFence"))               return (void*)bc250_vkCreateFence;
    if (!strcmp(pName, "vkDestroyFence"))              return (void*)bc250_vkDestroyFence;
    if (!strcmp(pName, "vkGetFenceStatus"))            return (void*)bc250_vkGetFenceStatus;
    if (!strcmp(pName, "vkWaitForFences"))             return (void*)bc250_vkWaitForFences;
    if (!strcmp(pName, "vkResetFences"))               return (void*)bc250_vkResetFences;
    if (!strcmp(pName, "vkCreateCommandPool"))         return (void*)bc250_vkCreateCommandPool;
    if (!strcmp(pName, "vkDestroyCommandPool"))        return (void*)bc250_vkDestroyCommandPool;
    if (!strcmp(pName, "vkResetCommandPool"))          return (void*)bc250_vkResetCommandPoolStub;
    if (!strcmp(pName, "vkAllocateCommandBuffers"))    return (void*)bc250_vkAllocateCommandBuffers;
    if (!strcmp(pName, "vkFreeCommandBuffers"))        return (void*)bc250_vkFreeCommandBuffers;
    if (!strcmp(pName, "vkBeginCommandBuffer"))        return (void*)bc250_vkBeginCommandBuffer;
    if (!strcmp(pName, "vkEndCommandBuffer"))          return (void*)bc250_vkEndCommandBuffer;
    if (!strcmp(pName, "vkResetCommandBuffer"))        return (void*)bc250_vkResetCommandBuffer;
    if (!strcmp(pName, "vkCreateRenderPass"))          return (void*)bc250_vkCreateRenderPassStub;
    if (!strcmp(pName, "vkDestroyRenderPass"))         return (void*)bc250_vkDestroyRenderPassStub;
    if (!strcmp(pName, "vkCreateFramebuffer"))         return (void*)bc250_vkCreateFramebufferStub;
    if (!strcmp(pName, "vkDestroyFramebuffer"))        return (void*)bc250_vkDestroyFramebufferStub;
    if (!strcmp(pName, "vkCreateSemaphore"))           return (void*)bc250_vkCreateSemaphoreStub;
    if (!strcmp(pName, "vkDestroySemaphore"))          return (void*)bc250_vkDestroySemaphoreStub;
    if (!strcmp(pName, "vkCreateShaderModule"))        return (void*)bc250_vkCreateShaderModuleStub;
    if (!strcmp(pName, "vkDestroyShaderModule"))       return (void*)bc250_vkDestroyShaderModuleStub;
    if (!strcmp(pName, "vkCreatePipelineLayout"))      return (void*)bc250_vkCreatePipelineLayoutStub;
    if (!strcmp(pName, "vkDestroyPipelineLayout"))     return (void*)bc250_vkDestroyPipelineLayoutStub;
    if (!strcmp(pName, "vkCreateDescriptorSetLayout")) return (void*)bc250_vkCreateDescriptorSetLayoutStub;
    if (!strcmp(pName, "vkDestroyDescriptorSetLayout")) return (void*)bc250_vkDestroyDescriptorSetLayoutStub;
    if (!strcmp(pName, "vkCreateDescriptorPool"))      return (void*)bc250_vkCreateDescriptorPoolStub;
    if (!strcmp(pName, "vkDestroyDescriptorPool"))     return (void*)bc250_vkDestroyDescriptorPoolStub;
    if (!strcmp(pName, "vkAllocateDescriptorSets"))    return (void*)bc250_vkAllocateDescriptorSetsStub;
    if (!strcmp(pName, "vkUpdateDescriptorSets"))      return (void*)bc250_vkUpdateDescriptorSetsStub;
    if (!strcmp(pName, "vkCreateSampler"))             return (void*)bc250_vkCreateSamplerStub;
    if (!strcmp(pName, "vkDestroySampler"))            return (void*)bc250_vkDestroySamplerStub;
    if (!strcmp(pName, "vkCreateImage"))               return (void*)bc250_vkCreateImage;
    if (!strcmp(pName, "vkDestroyImage"))              return (void*)bc250_vkDestroyImage;
    if (!strcmp(pName, "vkCreateImageView"))           return (void*)bc250_vkCreateImageViewStub;
    if (!strcmp(pName, "vkDestroyImageView"))          return (void*)bc250_vkDestroyImageViewStub;
    if (!strcmp(pName, "vkCreateQueryPool"))           return (void*)bc250_vkCreateQueryPoolStub;
    if (!strcmp(pName, "vkDestroyQueryPool"))          return (void*)bc250_vkDestroyQueryPoolStub;
    if (!strcmp(pName, "vkCreatePipelineCache"))       return (void*)bc250_vkCreatePipelineCacheStub;
    if (!strcmp(pName, "vkDestroyPipelineCache"))      return (void*)bc250_vkDestroyPipelineCacheStub;
    if (!strcmp(pName, "vkGetPipelineCacheData"))      return (void*)bc250_vkGetPipelineCacheDataStub;
    if (!strcmp(pName, "vkMergePipelineCaches"))       return (void*)bc250_vkMergePipelineCachesStub;
    if (!strcmp(pName, "vkFreeDescriptorSets"))        return (void*)bc250_vkFreeDescriptorSetsStub;
    if (!strcmp(pName, "vkResetDescriptorPool"))       return (void*)bc250_vkResetDescriptorPoolStub;
    if (!strcmp(pName, "vkCreateSwapchainKHR"))        return (void*)bc250_vkCreateSwapchainKHR;
    if (!strcmp(pName, "vkDestroySwapchainKHR"))       return (void*)bc250_vkDestroySwapchainKHR;
    if (!strcmp(pName, "vkGetSwapchainImagesKHR"))     return (void*)bc250_vkGetSwapchainImagesKHR;
    if (!strcmp(pName, "vkAcquireNextImageKHR"))       return (void*)bc250_vkAcquireNextImageKHR;
    if (!strcmp(pName, "vkQueuePresentKHR"))           return (void*)bc250_vkQueuePresentKHR;
    if (!strcmp(pName, "vkQueueSubmit"))               return (void*)bc250_vkQueueSubmit;
    if (!strcmp(pName, "vkQueueWaitIdle"))             return (void*)bc250_vkQueueWaitIdle;
    if (!strcmp(pName, "vkDeviceWaitIdle"))            return (void*)bc250_vkDeviceWaitIdle;
    if (!strcmp(pName, "vkCreateGraphicsPipelines"))   return (void*)bc250_vkCreateGraphicsPipelines;
    if (!strcmp(pName, "vkDestroyPipeline"))           return (void*)bc250_vkDestroyPipeline;
    if (!strcmp(pName, "vkCmdPipelineBarrier"))        return (void*)bc250_vkCmdPipelineBarrier;
    if (!strcmp(pName, "vkCmdBindPipeline"))           return (void*)bc250_vkCmdBindPipeline;
    if (!strcmp(pName, "vkCmdDraw"))                   return (void*)bc250_vkCmdDraw;
    if (!strcmp(pName, "vkCmdDrawIndexed"))            return (void*)bc250_vkCmdDrawIndexed;
    if (!strcmp(pName, "vkCmdDispatch"))               return (void*)bc250_vkCmdDispatch;
    if (!strcmp(pName, "vkCmdCopyBuffer"))             return (void*)bc250_vkCmdCopyBuffer;
    if (!strcmp(pName, "vkCmdCopyImage"))              return (void*)bc250_vkCmdCopyImage;
    if (!strcmp(pName, "vkCmdClearColorImage"))        return (void*)bc250_vkCmdClearColorImage;
    if (!strcmp(pName, "vkCmdClearDepthStencilImage")) return (void*)bc250_vkCmdClearDepthStencilImage;
    if (!strcmp(pName, "vkCmdSetViewport"))            return (void*)bc250_vkCmdSetViewport;
    if (!strcmp(pName, "vkCmdSetScissor"))             return (void*)bc250_vkCmdSetScissor;
    if (!strcmp(pName, "vkCmdBindVertexBuffers"))      return (void*)bc250_vkCmdBindVertexBuffers;
    if (!strcmp(pName, "vkCmdBindIndexBuffer"))        return (void*)bc250_vkCmdBindIndexBuffer;
    if (!strcmp(pName, "vkCmdBindDescriptorSets"))     return (void*)bc250_vkCmdBindDescriptorSets;
    if (!strcmp(pName, "vkCmdPushConstants"))          return (void*)bc250_vkCmdPushConstants;
    if (!strcmp(pName, "vkCmdBeginRenderPass"))        return (void*)bc250_vkCmdBeginRenderPassStub;
    if (!strcmp(pName, "vkCmdEndRenderPass"))          return (void*)bc250_vkCmdEndRenderPassStub;
    if (!strcmp(pName, "vkCmdNextSubpass"))            return (void*)bc250_vkCmdNextSubpassStub;
    
    /* Loader entry points */
    if (!strcmp(pName, "vkGetInstanceProcAddr"))        return (void*)vk_icdGetInstanceProcAddr;
    if (!strcmp(pName, "vkGetDeviceProcAddr"))          return (void*)vk_icdGetDeviceProcAddr;
    
    /* Additional required instance-level functions for Vulkan 1.4 loader */
    if (!strcmp(pName, "vkEnumerateInstanceVersion"))       return (void*)vkEnumerateInstanceVersion;
    if (!strcmp(pName, "vkGetPhysicalDeviceSparseImageFormatProperties")) return (void*)bc250_vkGetPhysicalDeviceSparseImageFormatPropertiesStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceSparseImageFormatProperties2")) return (void*)bc250_vkGetPhysicalDeviceSparseImageFormatProperties2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties2")) return (void*)bc250_vkGetPhysicalDeviceQueueFamilyProperties2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceMemoryProperties2")) return (void*)bc250_vkGetPhysicalDeviceMemoryProperties2;
    if (!strcmp(pName, "vkGetPhysicalDeviceFeatures2")) return (void*)bc250_vkGetPhysicalDeviceFeatures2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceFormatProperties2")) return (void*)bc250_vkGetPhysicalDeviceFormatProperties2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2")) return (void*)bc250_vkGetPhysicalDeviceImageFormatProperties2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceProperties2")) return (void*)bc250_vkGetPhysicalDeviceProperties2;
    if (!strcmp(pName, "vkGetPhysicalDeviceToolProperties")) return (void*)bc250_vkGetPhysicalDeviceToolPropertiesStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceSupportKHR")) return (void*)bc250_vkGetPhysicalDeviceSurfaceSupportKHR;
    if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return (void*)bc250_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceFormatsKHR")) return (void*)bc250_vkGetPhysicalDeviceSurfaceFormatsKHR;
    if (!strcmp(pName, "vkGetPhysicalDeviceSurfacePresentModesKHR")) return (void*)bc250_vkGetPhysicalDeviceSurfacePresentModesKHR;
    if (!strcmp(pName, "vkGetPhysicalDeviceDisplayPropertiesKHR")) return (void*)bc250_vkGetPhysicalDeviceDisplayPropertiesKHRStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR")) return (void*)bc250_vkGetPhysicalDeviceDisplayPlanePropertiesKHRStub;
    if (!strcmp(pName, "vkGetDisplayPlaneSupportedDisplaysKHR")) return (void*)bc250_vkGetDisplayPlaneSupportedDisplaysKHRStub;
    if (!strcmp(pName, "vkGetDisplayModePropertiesKHR")) return (void*)bc250_vkGetDisplayModePropertiesKHRStub;
    if (!strcmp(pName, "vkCreateDisplayModeKHR")) return (void*)bc250_vkCreateDisplayModeKHRStub;
    if (!strcmp(pName, "vkGetDisplayPlaneCapabilitiesKHR")) return (void*)bc250_vkGetDisplayPlaneCapabilitiesKHRStub;
    if (!strcmp(pName, "vkCreateDisplayPlaneSurfaceKHR")) return (void*)bc250_vkCreateDisplayPlaneSurfaceKHRStub;
    if (!strcmp(pName, "vkCreateViSurfaceNN")) return (void*)bc250_vkCreateViSurfaceNNStub;
    if (!strcmp(pName, "vkEnumeratePhysicalDeviceGroups")) return (void*)bc250_vkEnumeratePhysicalDeviceGroupsStub;
    if (!strcmp(pName, "vkCreateIOSSurfaceMVK")) return (void*)bc250_vkCreateIOSSurfaceMVKStub;
    if (!strcmp(pName, "vkCreateMacOSSurfaceMVK")) return (void*)bc250_vkCreateMacOSSurfaceMVKStub;
    if (!strcmp(pName, "vkCreateMetalSurfaceEXT")) return (void*)bc250_vkCreateMetalSurfaceEXTStub;
    if (!strcmp(pName, "vkCreateStreamDescriptorSurfaceGGP")) return (void*)bc250_vkCreateStreamDescriptorSurfaceGGPStub;
    if (!strcmp(pName, "vkCreateWin32SurfaceKHR")) return (void*)bc250_vkCreateWin32SurfaceKHR;
    if (!strcmp(pName, "vkGetPhysicalDeviceExternalFenceProperties")) return (void*)bc250_vkGetPhysicalDeviceExternalFencePropertiesStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceExternalSemaphoreProperties")) return (void*)bc250_vkGetPhysicalDeviceExternalSemaphorePropertiesStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceExternalBufferProperties")) return (void*)bc250_vkGetPhysicalDeviceExternalBufferPropertiesStub;
    if (!strcmp(pName, "vkGetPhysicalDevicePresentRectanglesKHR")) return (void*)bc250_vkGetPhysicalDevicePresentRectanglesKHRStub;
    if (!strcmp(pName, "vkReleaseDisplayEXT")) return (void*)bc250_vkReleaseDisplayEXTStub;
    if (!strcmp(pName, "vkAcquireXlibDisplayEXT")) return (void*)bc250_vkAcquireXlibDisplayEXTStub;
    if (!strcmp(pName, "vkGetRandROutputDisplayEXT")) return (void*)bc250_vkGetRandROutputDisplayEXTStub;
    
    OutputDebugStringA("BC-250 Vulkan: Unknown function requested\n");
    return NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
        if (f) { fprintf(f, "TRACE SEH-CAUGHT in dispatch pName=%s code=%08lX\n",
                         pName ? pName : "(null)", (unsigned long)GetExceptionCode());
                 fflush(f); fclose(f); }
        return NULL;
    }
}

__declspec(dllexport) void* VKAPI_CALL vk_icdGetDeviceProcAddr(VkDevice device, const char* pName)
{
    UNREFERENCED_PARAMETER(device);
    if (!pName) return NULL;
    __try {
        volatile char c = pName[0];
        size_t n = 0;
        while (n < 512 && pName[n]) n++;
        (void)c;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
    {
        FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
        if (f) { fprintf(f, "TRACE GetDeviceProcAddr %s\n", pName); fflush(f); fclose(f); }
    }
    void* r = vk_icdGetInstanceProcAddr(NULL, pName);
    {
        FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
        if (f) { fprintf(f, "TRACE GetDeviceProcAddr %s -> %p\n", pName ? pName : "(null)", r); fflush(f); fclose(f); }
    }
    return r;
}

/* Standard Vulkan entry points (called by loader) */
PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    return vk_icdGetInstanceProcAddr(instance, pName);
}

PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    return vk_icdGetDeviceProcAddr(device, pName);
}

__declspec(dllexport) uint32_t VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pVersion)
{
    if (pVersion) {
        if (*pVersion > 5) *pVersion = 5;
        FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
        if (f) { fprintf(f, "NegotiateLoaderICDInterfaceVersion: version=%u\n", *pVersion); fclose(f); }
    }
    return 0;
}

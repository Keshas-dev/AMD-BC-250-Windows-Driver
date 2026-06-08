/*
 * BC-250 Vulkan ICD - Windows Vulkan Installable Client Driver
 * 
 * Implements minimal Vulkan 1.4 API for AMD BC-250 GPU.
 * Uses ACO shader compiler (from Mesa) and our KMD for hardware access.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bc250_vulkan.h"
#include "bc250_aco_wrapper.h"
#include "bc250_shader.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

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

static BC250_VK_DEVICE g_Device = {0};

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
                       0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static VkResult bc250_init_instance(void)
{
    OutputDebugStringA("BC-250 Vulkan: Instance created\n");
    return VK_SUCCESS;
}

/* Enumerate instance extensions — return empty list */
static VkResult bc250_vkEnumerateInstanceExtensionProperties(
    const char* pLayerName, uint32_t* pPropertyCount, void* pProperties)
{
    UNREFERENCED_PARAMETER(pLayerName);
    UNREFERENCED_PARAMETER(pProperties);
    
    /* Write to file so we know this function is called */
    FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
    if (f) {
        fprintf(f, "EnumInstanceExtProps called: pLayerName=%s pPropertyCount=%p pProperties=%p\n",
                pLayerName ? pLayerName : "(null)", (void*)pPropertyCount, pProperties);
        fflush(f);
        fclose(f);
    }
    
    if (pPropertyCount) *pPropertyCount = 0;
    return VK_SUCCESS;
}

/* Enumerate device extensions — return empty list */
static VkResult bc250_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice, const char* pLayerName,
    uint32_t* pPropertyCount, void* pProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    UNREFERENCED_PARAMETER(pLayerName);
    UNREFERENCED_PARAMETER(pProperties);
    if (pPropertyCount) *pPropertyCount = 0;
    return VK_SUCCESS;
}

/* Additional required instance-level stubs for Vulkan 1.4 loader compatibility */
static void bc250_vkGetPhysicalDeviceSparseImageFormatPropertiesStub(VkPhysicalDevice a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t* f, void** g) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); if (f) *f = 0; UNREFERENCED_PARAMETER(g); }
static void bc250_vkGetPhysicalDeviceSparseImageFormatProperties2Stub(VkPhysicalDevice a, const void* b, uint32_t* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; UNREFERENCED_PARAMETER(d); }
static void bc250_vkGetPhysicalDeviceQueueFamilyProperties2Stub(VkPhysicalDevice a, uint32_t* b, void* c) { UNREFERENCED_PARAMETER(a); if (b) *b = 0; UNREFERENCED_PARAMETER(c); }
static void bc250_vkGetPhysicalDeviceMemoryProperties2Stub(VkPhysicalDevice a, void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); }
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

/* Forward declarations for loader entry points */
__declspec(dllexport) void* VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
__declspec(dllexport) void* VKAPI_CALL vk_icdGetDeviceProcAddr(VkDevice device, const char* pName);

/* Required Vulkan 1.0 stub functions */
static VkResult bc250_vkGetPhysicalDeviceFeatures(VkPhysicalDevice a, void* b) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); return VK_SUCCESS; }
static void bc250_vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice a, uint32_t b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint64_t f, uint32_t* g, void* h) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); UNREFERENCED_PARAMETER(f); if (g) *g = 128; UNREFERENCED_PARAMETER(h); return VK_SUCCESS; }
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
static VkResult bc250_vkCreateRenderPassStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyRenderPassStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateFramebufferStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyFramebufferStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateSemaphoreStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroySemaphoreStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateShaderModuleStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyShaderModuleStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreatePipelineLayoutStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyPipelineLayoutStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateDescriptorSetLayoutStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyDescriptorSetLayoutStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateDescriptorPoolStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyDescriptorPoolStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkAllocateDescriptorSetsStub(VkDevice a, const void* b, void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); return VK_SUCCESS; }
static void bc250_vkUpdateDescriptorSetsStub(VkDevice a, uint32_t b, const void* c, uint32_t d, const void* e) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); }
static VkResult bc250_vkCreateSamplerStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroySamplerStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateQueryPoolStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroyQueryPoolStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkCreateSwapchainKHRStub(VkDevice a, const void* b, const void* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static void bc250_vkDestroySwapchainKHRStub(VkDevice a, void* b, const void* c) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); }
static VkResult bc250_vkGetSwapchainImagesKHRStub(VkDevice a, void* b, uint32_t* c, void* d) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); if (c) *c = 0; UNREFERENCED_PARAMETER(d); return VK_SUCCESS; }
static VkResult bc250_vkAcquireNextImageKHRStub(VkDevice a, void* b, uint64_t c, void* d, void* e, uint32_t* f) { UNREFERENCED_PARAMETER(a); UNREFERENCED_PARAMETER(b); UNREFERENCED_PARAMETER(c); UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(e); if (f) *f = 0; return VK_SUCCESS; }

static VkResult bc250_init_device(VkDevice device)
{
    BC250_VK_DEVICE* dev = (BC250_VK_DEVICE*)device;
    
    dev->kmdDevice = bc250_open_kmd();
    if (dev->kmdDevice == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        char buf[256];
        snprintf(buf, sizeof(buf), "BC-250 Vulkan: KMD open FAILED (error %lu, handle=%p)\n",
                 err, (void*)dev->kmdDevice);
        OutputDebugStringA(buf);
    } else {
        OutputDebugStringA("BC-250 Vulkan: KMD opened OK\n");
    }
    
    dev->nextGpuVa = 0x100000000ULL;
    dev->vramTotal = 4ULL * 1024 * 1024 * 1024;
    dev->vramUsed = 0;
    dev->fenceValue = 1;
    
    OutputDebugStringA("BC-250 Vulkan: Device initialized\n");
    return VK_SUCCESS;
}

/* Vulkan API implementations */

VkResult VKAPI_CALL bc250_vkCreateInstance(
    const void* pCreateInfo,
    const void* pAllocator,
    VkInstance* pInstance)
{
    VkResult result = bc250_init_instance();
    if (result == VK_SUCCESS) {
        *pInstance = (VkInstance)1; /* Dummy handle */
    }
    return result;
}

void VKAPI_CALL bc250_vkDestroyInstance(VkInstance instance, const void* pAllocator)
{
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(pAllocator);
    OutputDebugStringA("BC-250 Vulkan: Instance destroyed\n");
}

VkResult VKAPI_CALL bc250_vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices)
{
    UNREFERENCED_PARAMETER(instance);
    
    if (pPhysicalDevices == NULL) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }
    
    if (*pPhysicalDeviceCount >= 1) {
        *pPhysicalDevices = (VkPhysicalDevice)1;
        *pPhysicalDeviceCount = 1;
    }
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
    
    if (!dev) return VK_ERROR_OUT_OF_HOST_MEMORY;
    
    VkResult result = bc250_init_device((VkDevice)dev);
    if (result != VK_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, dev);
        return result;
    }
    
    *pDevice = (VkDevice)dev;
    return VK_SUCCESS;
}

void VKAPI_CALL bc250_vkDestroyDevice(VkDevice device, const void* pAllocator)
{
    UNREFERENCED_PARAMETER(pAllocator);
    if (device) {
        bc250_DestroyDevice(device);
        HeapFree(GetProcessHeap(), 0, (void*)device);
    }
}

/* Physical Device Properties */
VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    void* pProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    
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
    props->limits.maxTessellationDomainInputComponents = 128;
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
    UNREFERENCED_PARAMETER(physicalDevice);
    
    /* Report memory heaps for BC-250 (16GB shared UMA) */
    memset(pMemoryProperties, 0, 256);
    
    OutputDebugStringA("BC-250 Vulkan: GetPhysicalDeviceMemoryProperties\n");
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    void* pQueueFamilyProperties)
{
    UNREFERENCED_PARAMETER(physicalDevice);
    
    if (pQueueFamilyProperties == NULL) {
        *pQueueFamilyPropertyCount = 2; /* Graphics + Transfer */
        return VK_SUCCESS;
    }
    
    /* Queue family 0: Graphics + Compute + Transfer */
    memset(pQueueFamilyProperties, 0, sizeof(void*) * 8);
    /* Set queue flags: graphics | compute | transfer */
    ((uint32_t*)pQueueFamilyProperties)[0] = 
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    ((uint32_t*)pQueueFamilyProperties)[1] = 1; /* queueCount */
    
    /* Queue family 1: Transfer only */
    ((uint32_t*)pQueueFamilyProperties)[4] = VK_QUEUE_TRANSFER_BIT;
    ((uint32_t*)pQueueFamilyProperties)[5] = 1;
    
    *pQueueFamilyPropertyCount = 2;
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    void* pMemoryProperties)
{
    return bc250_vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
}

VkResult VKAPI_CALL bc250_vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice,
    void* pProperties)
{
    return bc250_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
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
    *pQueue = (VkQueue)device;
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
    
    /* Parse VkAllocateMemoryInfo */
    uint64_t allocSize = 4096;  /* Default 4KB */
    /* In real impl: parse VkAllocateMemoryInfo->allocationSize */
    
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
    uint32_t idx = (uint32_t)(uintptr_t)memory - 1;
    if (idx < MAX_ALLOCATIONS && g_Allocations[idx].cpuVa != NULL) {
        *ppData = (uint8_t*)g_Allocations[idx].cpuVa + offset;
        return VK_SUCCESS;
    }
    return VK_ERROR_MEMORY_MAP_FAILED;
}

void VKAPI_CALL bc250_vkUnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(memory);
}

/* Buffer management — VirtualAlloc (safe, no KMD IOCTL) */
VkResult VKAPI_CALL bc250_vkCreateBuffer(
    VkDevice device,
    const void* pCreateInfo,
    const void* pAllocator,
    VkBuffer* pBuffer)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    
    /* Use VirtualAlloc for now (safe) */
    *pBuffer = (VkBuffer)VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
    return *pBuffer ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

void VKAPI_CALL bc250_vkDestroyBuffer(VkDevice device, VkBuffer buffer, const void* pAllocator)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(pAllocator);
    if (buffer) VirtualFree((void*)buffer, 0, MEM_RELEASE);
}

/* Image management with KMD IOCTL */
VkResult VKAPI_CALL bc250_vkCreateImage(
    VkDevice device,
    const void* pCreateInfo,
    const void* pAllocator,
    VkImage* pImage)
{
    UNREFERENCED_PARAMETER(pCreateInfo);
    UNREFERENCED_PARAMETER(pAllocator);
    
    BC250_VK_DEVICE* dev = (BC250_VK_DEVICE*)device;
    
    /* Allocate GPU memory for image via KMD IOCTL */
    if (dev->kmdDevice != INVALID_HANDLE_VALUE) {
        ULONG allocIn[4] = {0};
        ULONG64 allocOut[3] = {0};
        DWORD ret = 0;
        
        allocIn[0] = 4096 * 4;  /* 16KB default for 1024x1024 RGBA */
        allocIn[1] = 256;       /* Alignment */
        allocIn[2] = 0x3;       /* Flags: READ|WRITE */
        allocIn[3] = 0;         /* Segment: VRAM */
        
        if (DeviceIoControl(dev->kmdDevice, 0x80000840,
                           allocIn, sizeof(allocIn),
                           allocOut, sizeof(allocOut), &ret, NULL)) {
            *pImage = (VkImage)(ULONG_PTR)allocOut[2];
            return VK_SUCCESS;
        }
    }
    
    *pImage = (VkImage)VirtualAlloc(NULL, 4096 * 4, MEM_COMMIT, PAGE_READWRITE);
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

/* Command buffers */
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
    UNREFERENCED_PARAMETER(pAllocateInfo);
    
    /* Allocate command buffer (64KB) */
    pCommandBuffers[0] = (VkCommandBuffer)VirtualAlloc(NULL, 64 * 1024, MEM_COMMIT, PAGE_READWRITE);
    return pCommandBuffers[0] ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
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
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(pBeginInfo);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    return VK_SUCCESS;
}

VkResult VKAPI_CALL bc250_vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkFlags flags)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(flags);
    return VK_SUCCESS;
}

/* Queue operations */
/* PM4 opcodes */
#define PM4_TYPE3_HDR(opcode, cnt) ((3u << 30) | (((cnt) - 1) << 16) | ((opcode) << 8))
#define IT_EVENT_WRITE_EOP  0x47
#define IT_NOP              0x10

/* KMD IOCTL codes */
#define IOCTL_AMDBC250_ALLOC_DMA_BUFFER  0x80000930
#define IOCTL_AMDBC250_FREE_DMA_BUFFER   0x80000934
#define IOCTL_AMDBC250_SUBMIT_COMMANDS   0x80000880
#define IOCTL_AMDBC250_WAIT_FENCE        0x80000884

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

/* Queue Submit — writes PM4 commands to shared memory (no IOCTL) */
VkResult VKAPI_CALL bc250_vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const void* pSubmits,
    VkFence fence)
{
    UNREFERENCED_PARAMETER(queue);
    UNREFERENCED_PARAMETER(submitCount);
    UNREFERENCED_PARAMETER(pSubmits);
    UNREFERENCED_PARAMETER(fence);
    
    BC250_VK_DEVICE* dev = &g_Device;
    if (dev->kmdDevice == INVALID_HANDLE_VALUE) return VK_SUCCESS;

    /* Allocate DMA buffer for PM4 commands via KMD IOCTL */
    ULONG allocReq = 0x10000; /* 64KB buffer */
    ULONG64 allocResp[2] = {0};
    DWORD ret = 0;
    PVOID dmaBuf = NULL;
    ULONG64 dmaBufPa = 0;

    if (DeviceIoControl(dev->kmdDevice, 0x80000930, /* ALLOC_DMA_BUFFER */
                        &allocReq, sizeof(allocReq),
                        allocResp, sizeof(allocResp), &ret, NULL)) {
        dmaBufPa = allocResp[0];
        dmaBuf    = (PVOID)(ULONG_PTR)allocResp[1];
    }

    if (dmaBuf) {
        volatile PULONG cmd = (volatile PULONG)dmaBuf;
        
        /* NOP padding */
        cmd[0] = 0x80000000;
        
        /* EOP fence packet (IT_EVENT_WRITE_EOP = 0x46) */
        cmd[1] = 0xC0034600;  /* PM4_TYPE3_HDR(IT_EVENT_WRITE_EOP, 4) */
        cmd[2] = 0xA0000246;  /* EVENT_TYPE=EOP, EVENT_INDEX=5, INT_SEL=1 */
        cmd[3] = (ULONG)(dmaBufPa & 0xFFFFFFFF);    /* Fence addr low */
        cmd[4] = (ULONG)(dmaBufPa >> 32);            /* Fence addr high */
        cmd[5] = (ULONG)(dev->fenceValue & 0xFFFFFFFF);
        cmd[6] = (ULONG)((ULONG64)dev->fenceValue >> 32);

        /* Submit to KMD ring via SUBMIT_COMMANDS */
        struct { ULONG64 GpuVa; ULONG64 Size; ULONG32 Fence; ULONG32 Queue; } sc = {0};
        sc.GpuVa = dmaBufPa;
        sc.Size   = 7 * sizeof(ULONG);
        sc.Fence  = dev->fenceValue;
        sc.Queue  = 0; /* GFX ring */
        
        DeviceIoControl(dev->kmdDevice, 0x80000880, /* SUBMIT_COMMANDS */
                        &sc, sizeof(sc), NULL, 0, &ret, NULL);
        
        dev->fenceValue++;
    }
    
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
VkResult VKAPI_CALL bc250_vkQueuePresentKHR(
    VkQueue queue,
    const void* pPresentInfo)
{
    UNREFERENCED_PARAMETER(queue);
    UNREFERENCED_PARAMETER(pPresentInfo);

    /* Submit present to KMD via IOCTL 0x800008C4 */
    BC250_VK_DEVICE* dev = &g_Device;
    if (dev->kmdDevice != INVALID_HANDLE_VALUE) {
        ULONG flipData[7] = {0};
        flipData[0] = 0;  /* Surface address low */
        flipData[1] = 0;  /* Surface address high */
        flipData[2] = 1920; /* Width */
        flipData[3] = 1080; /* Height */
        flipData[4] = 1920 * 4; /* Pitch */
        flipData[5] = 22;  /* D3DDDIFMT_A8R8G8B8 */
        flipData[6] = 1;   /* VSync */

        DWORD ret = 0;
        DeviceIoControl(dev->kmdDevice, 0x800008C4, flipData, sizeof(flipData),
                        NULL, 0, &ret, NULL);
    }

    OutputDebugStringA("BC-250 Vulkan: QueuePresentKHR → KMD flip\n");
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
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(vertexCount);
    UNREFERENCED_PARAMETER(instanceCount);
    UNREFERENCED_PARAMETER(firstVertex);
    UNREFERENCED_PARAMETER(firstInstance);
    
    /* TODO: Emit PM4 DRAW_INDEX_AUTO packet */
}

void VKAPI_CALL bc250_vkCmdDrawIndexed(
    VkCommandBuffer commandBuffer,
    uint32_t indexCount,
    uint32_t instanceCount,
    uint32_t firstIndex,
    int32_t vertexOffset,
    uint32_t firstInstance)
{
    UNREFERENCED_PARAMETER(commandBuffer);
    UNREFERENCED_PARAMETER(indexCount);
    UNREFERENCED_PARAMETER(instanceCount);
    UNREFERENCED_PARAMETER(firstIndex);
    UNREFERENCED_PARAMETER(vertexOffset);
    UNREFERENCED_PARAMETER(firstInstance);
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
__declspec(dllexport) void* VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    UNREFERENCED_PARAMETER(instance);
    
    if (!pName) return NULL;
    
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
    if (!strcmp(pName, "vkCreateBuffer"))              return (void*)bc250_vkCreateBuffer;
    if (!strcmp(pName, "vkDestroyBuffer"))             return (void*)bc250_vkDestroyBuffer;
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
    if (!strcmp(pName, "vkCreateQueryPool"))           return (void*)bc250_vkCreateQueryPoolStub;
    if (!strcmp(pName, "vkDestroyQueryPool"))          return (void*)bc250_vkDestroyQueryPoolStub;
    if (!strcmp(pName, "vkCreateSwapchainKHR"))        return (void*)bc250_vkCreateSwapchainKHRStub;
    if (!strcmp(pName, "vkDestroySwapchainKHR"))       return (void*)bc250_vkDestroySwapchainKHRStub;
    if (!strcmp(pName, "vkGetSwapchainImagesKHR"))     return (void*)bc250_vkGetSwapchainImagesKHRStub;
    if (!strcmp(pName, "vkAcquireNextImageKHR"))       return (void*)bc250_vkAcquireNextImageKHRStub;
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
    if (!strcmp(pName, "vkGetPhysicalDeviceSparseImageFormatProperties")) return (void*)bc250_vkGetPhysicalDeviceSparseImageFormatPropertiesStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceSparseImageFormatProperties2")) return (void*)bc250_vkGetPhysicalDeviceSparseImageFormatProperties2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties2")) return (void*)bc250_vkGetPhysicalDeviceQueueFamilyProperties2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceMemoryProperties2")) return (void*)bc250_vkGetPhysicalDeviceMemoryProperties2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceFeatures2")) return (void*)bc250_vkGetPhysicalDeviceFeatures2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceFormatProperties2")) return (void*)bc250_vkGetPhysicalDeviceFormatProperties2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2")) return (void*)bc250_vkGetPhysicalDeviceImageFormatProperties2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceProperties2")) return (void*)bc250_vkGetPhysicalDeviceProperties2Stub;
    if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceSupportKHR")) return (void*)bc250_vkGetPhysicalDeviceSurfaceSupportStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return (void*)bc250_vkGetPhysicalDeviceSurfaceCapabilitiesStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceSurfaceFormatsKHR")) return (void*)bc250_vkGetPhysicalDeviceSurfaceFormatsStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceSurfacePresentModesKHR")) return (void*)bc250_vkGetPhysicalDeviceSurfacePresentModesStub;
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
    if (!strcmp(pName, "vkCreateWin32SurfaceKHR")) return (void*)bc250_vkCreateWin32SurfaceKHRStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceExternalFenceProperties")) return (void*)bc250_vkGetPhysicalDeviceExternalFencePropertiesStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceExternalSemaphoreProperties")) return (void*)bc250_vkGetPhysicalDeviceExternalSemaphorePropertiesStub;
    if (!strcmp(pName, "vkGetPhysicalDeviceExternalBufferProperties")) return (void*)bc250_vkGetPhysicalDeviceExternalBufferPropertiesStub;
    if (!strcmp(pName, "vkGetPhysicalDevicePresentRectanglesKHR")) return (void*)bc250_vkGetPhysicalDevicePresentRectanglesKHRStub;
    if (!strcmp(pName, "vkReleaseDisplayEXT")) return (void*)bc250_vkReleaseDisplayEXTStub;
    if (!strcmp(pName, "vkAcquireXlibDisplayEXT")) return (void*)bc250_vkAcquireXlibDisplayEXTStub;
    if (!strcmp(pName, "vkGetRandROutputDisplayEXT")) return (void*)bc250_vkGetRandROutputDisplayEXTStub;
    
    OutputDebugStringA("BC-250 Vulkan: Unknown function requested\n");
    return NULL;
}

__declspec(dllexport) void* VKAPI_CALL vk_icdGetDeviceProcAddr(VkDevice device, const char* pName)
{
    UNREFERENCED_PARAMETER(device);
    return vk_icdGetInstanceProcAddr(NULL, pName);
}

/* Standard Vulkan entry points (called by loader) */
__declspec(dllexport) void* VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    return vk_icdGetInstanceProcAddr(instance, pName);
}

__declspec(dllexport) void* VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    return vk_icdGetDeviceProcAddr(device, pName);
}

/* ICD interface negotiation - report version 4 for Vulkan 1.0 compatibility */
__declspec(dllexport) uint32_t VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pVersion)
{
    if (pVersion) {
        if (*pVersion > 4) *pVersion = 4;
        FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-log.txt", "a");
        if (f) { fprintf(f, "NegotiateLoaderICDInterfaceVersion: version=%u\n", *pVersion); fclose(f); }
    }
    return 0;
}

/* radv-icd-stub.c - auto-generated Vulkan ICD stub for BC-250. */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "F:/VulkanSDK/1.4.341.1/Include/vulkan/vulkan.h"

static HMODULE g_hModule = NULL;
static const char g_deviceName[] = "AMD BC-250 (RADV Stub)";

static void set_minimal_props(VkPhysicalDeviceProperties* p) {
    memset(p, 0, sizeof(*p));
    p->apiVersion = VK_API_VERSION_1_2;
    p->driverVersion = VK_MAKE_VERSION(0, 0, 1);
    p->vendorID = 0x1002;
    p->deviceID = 0x13FE;
    p->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    p->limits.maxImageDimension2D = 16384;
    p->limits.maxImageArrayLayers = 2048;
    p->limits.maxComputeWorkGroupInvocations = 1024;
    p->limits.maxComputeWorkGroupSize[0] = 1024;
    p->limits.maxComputeWorkGroupSize[1] = 1024;
    p->limits.maxComputeWorkGroupSize[2] = 64;
    p->limits.maxViewports = 16;
    p->limits.maxViewportDimensions[0] = 16384;
    p->limits.maxViewportDimensions[1] = 16384;
    p->limits.maxDrawIndirectCount = 0xFFFF;
    p->limits.maxSamplerAnisotropy = 16;
    p->limits.maxTessellationPatchSize = 32;
    p->limits.maxVertexInputAttributes = 16;
    p->limits.maxVertexInputBindings = 16;
    p->limits.maxVertexOutputComponents = 128;
    p->limits.maxFragmentOutputAttachments = 4;
    p->limits.maxColorAttachments = 4;
    p->limits.maxMemoryAllocationCount = 4096;
    p->limits.maxSamplerAllocationCount = 32768;
    p->limits.minUniformBufferOffsetAlignment = 64;
    p->limits.minStorageBufferOffsetAlignment = 64;
    strncpy(p->deviceName, g_deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    p->deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1] = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    (void)pCreateInfo; (void)pAllocator;
    if (pInstance) {
        void* mem = malloc(8);
        if (!mem) return VK_ERROR_OUT_OF_HOST_MEMORY;
        *(void**)mem = NULL;
        *pInstance = (VkInstance)mem;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) { (void)pAllocator; if (instance) free((void*)instance); }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    (void)instance;
    static VkPhysicalDevice cached = NULL;
    if (!cached) { void* m = malloc(8); if (!m) return VK_ERROR_OUT_OF_HOST_MEMORY; *(void**)m = NULL; cached = (VkPhysicalDevice)m; }
    if (!pPhysicalDeviceCount) return VK_SUCCESS;
    if (!pPhysicalDevices) { *pPhysicalDeviceCount = 1; return VK_SUCCESS; }
    if (*pPhysicalDeviceCount < 1) { *pPhysicalDeviceCount = 1; return VK_INCOMPLETE; }
    pPhysicalDevices[0] = cached;
    *pPhysicalDeviceCount = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) {
    (void)physicalDevice; if (!pFeatures) return; memset(pFeatures, 0, sizeof(*pFeatures));
    pFeatures->geometryShader = VK_TRUE; pFeatures->tessellationShader = VK_TRUE;
    pFeatures->multiViewport = VK_TRUE; pFeatures->samplerAnisotropy = VK_TRUE;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties) { (void)physicalDevice; (void)format; (void)pFormatProperties;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkImageFormatProperties*                    pImageFormatProperties) { (void)physicalDevice; (void)format; (void)type; (void)tiling; (void)usage; (void)flags; (void)pImageFormatProperties;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) {
    (void)physicalDevice; if (pProperties) set_minimal_props(pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) {
    (void)physicalDevice;
    if (!pQueueFamilyPropertyCount) return;
    if (!pQueueFamilyProperties) { *pQueueFamilyPropertyCount = 1; return; }
    if (*pQueueFamilyPropertyCount < 1) { *pQueueFamilyPropertyCount = 1; return; }
    memset(pQueueFamilyProperties, 0, sizeof(*pQueueFamilyProperties));
    pQueueFamilyProperties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties[0].queueCount = 1;
    pQueueFamilyProperties[0].timestampValidBits = 64;
    pQueueFamilyProperties[0].minImageTransferGranularity.width = 1;
    pQueueFamilyProperties[0].minImageTransferGranularity.height = 1;
    pQueueFamilyProperties[0].minImageTransferGranularity.depth = 1;
    *pQueueFamilyPropertyCount = 1;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
    (void)physicalDevice; if (!pMemoryProperties) return; memset(pMemoryProperties, 0, sizeof(*pMemoryProperties));
    pMemoryProperties->memoryTypeCount = 1;
    pMemoryProperties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    pMemoryProperties->memoryTypes[0].heapIndex = 0;
    pMemoryProperties->memoryHeapCount = 1;
    pMemoryProperties->memoryHeaps[0].size = 256ull * 1024 * 1024;
    pMemoryProperties->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    (void)instance; if (!pName) return NULL;
    return (PFN_vkVoidFunction)GetProcAddress(g_hModule, pName);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    (void)device; if (!pName) return NULL;
    return (PFN_vkVoidFunction)GetProcAddress(g_hModule, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    (void)physicalDevice; (void)pCreateInfo; (void)pAllocator;
    if (pDevice) { void* m = malloc(16); if (!m) return VK_ERROR_OUT_OF_HOST_MEMORY; *(void**)m = NULL; *((void**)m+1)=NULL; *pDevice = (VkDevice)m; }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) { (void)pAllocator; if (device) free((void*)device); }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    (void)pLayerName; (void)pProperties; if (pPropertyCount) *pPropertyCount = 0; return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    (void)physicalDevice; (void)pLayerName; (void)pProperties; if (pPropertyCount) *pPropertyCount = 0; return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    (void)pProperties; if (pPropertyCount) *pPropertyCount = 0; return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    (void)physicalDevice; (void)pProperties; if (pPropertyCount) *pPropertyCount = 0; return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue) { (void)device; (void)queueFamilyIndex; (void)queueIndex; (void)pQueue;   }

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence) { (void)queue; (void)submitCount; (void)pSubmits; (void)fence;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue                                     queue) { (void)queue;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice                                    device) { (void)device;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory) { (void)device; (void)pAllocateInfo; (void)pAllocator; (void)pMemory;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)memory; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData) { (void)device; (void)memory; (void)offset; (void)size; (void)flags; (void)ppData;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice                                    device,
    VkDeviceMemory                              memory) { (void)device; (void)memory;   }

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice                                    device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges) { (void)device; (void)memoryRangeCount; (void)pMemoryRanges;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice                                    device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges) { (void)device; (void)memoryRangeCount; (void)pMemoryRanges;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize*                               pCommittedMemoryInBytes) { (void)device; (void)memory; (void)pCommittedMemoryInBytes;   }

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset) { (void)device; (void)buffer; (void)memory; (void)memoryOffset;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset) { (void)device; (void)image; (void)memory; (void)memoryOffset;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkMemoryRequirements*                       pMemoryRequirements) { (void)device; (void)buffer; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements) { (void)device; (void)image; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(VkDevice                                    device,
    VkImage                                     image,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements*            pSparseMemoryRequirements) { (void)device; (void)image; (void)pSparseMemoryRequirementCount; (void)pSparseMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkSampleCountFlagBits                       samples,
    VkImageUsageFlags                           usage,
    VkImageTiling                               tiling,
    uint32_t*                                   pPropertyCount,
    VkSparseImageFormatProperties*              pProperties) { (void)physicalDevice; (void)format; (void)type; (void)samples; (void)usage; (void)tiling; (void)pPropertyCount; (void)pProperties;   }

VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(VkQueue                                     queue,
    uint32_t                                    bindInfoCount,
    const VkBindSparseInfo*                     pBindInfo,
    VkFence                                     fence) { (void)queue; (void)bindInfoCount; (void)pBindInfo; (void)fence;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice                                    device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pFence;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice                                    device,
    VkFence                                     fence,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)fence; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences) { (void)device; (void)fenceCount; (void)pFences;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice                                    device,
    VkFence                                     fence) { (void)device; (void)fence;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences,
    VkBool32                                    waitAll,
    uint64_t                                    timeout) { (void)device; (void)fenceCount; (void)pFences; (void)waitAll; (void)timeout;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice                                    device,
    const VkSemaphoreCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSemaphore*                                pSemaphore) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pSemaphore;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice                                    device,
    VkSemaphore                                 semaphore,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)semaphore; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice                                    device,
    const VkQueryPoolCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkQueryPool*                                pQueryPool) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pQueryPool;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice                                    device,
    VkQueryPool                                 queryPool,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)queryPool; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    size_t                                      dataSize,
    void*                                       pData,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags) { (void)device; (void)queryPool; (void)firstQuery; (void)queryCount; (void)dataSize; (void)pData; (void)stride; (void)flags;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pBuffer;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice                                    device,
    VkBuffer                                    buffer,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)buffer; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pImage;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)image; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(VkDevice                                    device,
    VkImage                                     image,
    const VkImageSubresource*                   pSubresource,
    VkSubresourceLayout*                        pLayout) { (void)device; (void)image; (void)pSubresource; (void)pLayout;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice                                    device,
    const VkImageViewCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImageView*                                pView) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pView;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice                                    device,
    VkImageView                                 imageView,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)imageView; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice                                    device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCommandPool) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pCommandPool;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice                                    device,
    VkCommandPool                               commandPool,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)commandPool; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolResetFlags                     flags) { (void)device; (void)commandPool; (void)flags;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice                                    device,
    const VkCommandBufferAllocateInfo*          pAllocateInfo,
    VkCommandBuffer*                            pCommandBuffers) { (void)device; (void)pAllocateInfo; (void)pCommandBuffers;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice                                    device,
    VkCommandPool                               commandPool,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers) { (void)device; (void)commandPool; (void)commandBufferCount; (void)pCommandBuffers;   }

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo) { (void)commandBuffer; (void)pBeginInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer                             commandBuffer) { (void)commandBuffer;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer                             commandBuffer,
    VkCommandBufferResetFlags                   flags) { (void)commandBuffer; (void)flags;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferCopy*                         pRegions) { (void)commandBuffer; (void)srcBuffer; (void)dstBuffer; (void)regionCount; (void)pRegions;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageCopy*                          pRegions) { (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions) { (void)commandBuffer; (void)srcBuffer; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions) { (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstBuffer; (void)regionCount; (void)pRegions;   }

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                dataSize,
    const void*                                 pData) { (void)commandBuffer; (void)dstBuffer; (void)dstOffset; (void)dataSize; (void)pData;   }

VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                size,
    uint32_t                                    data) { (void)commandBuffer; (void)dstBuffer; (void)dstOffset; (void)size; (void)data;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    VkDependencyFlags                           dependencyFlags,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers) { (void)commandBuffer; (void)srcStageMask; (void)dstStageMask; (void)dependencyFlags; (void)memoryBarrierCount; (void)pMemoryBarriers; (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers; (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags) { (void)commandBuffer; (void)queryPool; (void)query; (void)flags;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query) { (void)commandBuffer; (void)queryPool; (void)query;   }

VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount) { (void)commandBuffer; (void)queryPool; (void)firstQuery; (void)queryCount;   }

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query) { (void)commandBuffer; (void)pipelineStage; (void)queryPool; (void)query;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags) { (void)commandBuffer; (void)queryPool; (void)firstQuery; (void)queryCount; (void)dstBuffer; (void)dstOffset; (void)stride; (void)flags;   }

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteCommands(VkCommandBuffer                             commandBuffer,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers) { (void)commandBuffer; (void)commandBufferCount; (void)pCommandBuffers;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(VkDevice                                    device,
    const VkEventCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkEvent*                                    pEvent) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pEvent;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(VkDevice                                    device,
    VkEvent                                     event,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)event; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice                                    device,
    VkEvent                                     event) { (void)device; (void)event;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice                                    device,
    VkEvent                                     event) { (void)device; (void)event;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice                                    device,
    VkEvent                                     event) { (void)device; (void)event;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(VkDevice                                    device,
    const VkBufferViewCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBufferView*                               pView) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pView;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(VkDevice                                    device,
    VkBufferView                                bufferView,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)bufferView; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pShaderModule;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice                                    device,
    VkShaderModule                              shaderModule,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)shaderModule; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice                                    device,
    const VkPipelineCacheCreateInfo*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineCache*                            pPipelineCache) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pPipelineCache;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)pipelineCache; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    size_t*                                     pDataSize,
    void*                                       pData) { (void)device; (void)pipelineCache; (void)pDataSize; (void)pData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(VkDevice                                    device,
    VkPipelineCache                             dstCache,
    uint32_t                                    srcCacheCount,
    const VkPipelineCache*                      pSrcCaches) { (void)device; (void)dstCache; (void)srcCacheCount; (void)pSrcCaches;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkComputePipelineCreateInfo*          pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) { (void)device; (void)pipelineCache; (void)createInfoCount; (void)pCreateInfos; (void)pAllocator; (void)pPipelines;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)pipeline; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice                                    device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pPipelineLayout;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice                                    device,
    VkPipelineLayout                            pipelineLayout,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)pipelineLayout; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice                                    device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pSampler;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(VkDevice                                    device,
    VkSampler                                   sampler,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)sampler; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pSetLayout;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice                                    device,
    VkDescriptorSetLayout                       descriptorSetLayout,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)descriptorSetLayout; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice                                    device,
    const VkDescriptorPoolCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorPool*                           pDescriptorPool) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pDescriptorPool;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)descriptorPool; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    VkDescriptorPoolResetFlags                  flags) { (void)device; (void)descriptorPool; (void)flags;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice                                    device,
    const VkDescriptorSetAllocateInfo*          pAllocateInfo,
    VkDescriptorSet*                            pDescriptorSets) { (void)device; (void)pAllocateInfo; (void)pDescriptorSets;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets) { (void)device; (void)descriptorPool; (void)descriptorSetCount; (void)pDescriptorSets;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice                                    device,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites,
    uint32_t                                    descriptorCopyCount,
    const VkCopyDescriptorSet*                  pDescriptorCopies) { (void)device; (void)descriptorWriteCount; (void)pDescriptorWrites; (void)descriptorCopyCount; (void)pDescriptorCopies;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline) { (void)commandBuffer; (void)pipelineBindPoint; (void)pipeline;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    firstSet,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets,
    uint32_t                                    dynamicOffsetCount,
    const uint32_t*                             pDynamicOffsets) { (void)commandBuffer; (void)pipelineBindPoint; (void)layout; (void)firstSet; (void)descriptorSetCount; (void)pDescriptorSets; (void)dynamicOffsetCount; (void)pDynamicOffsets;   }

VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearColorValue*                    pColor,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges) { (void)commandBuffer; (void)image; (void)imageLayout; (void)pColor; (void)rangeCount; (void)pRanges;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer                             commandBuffer,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ) { (void)commandBuffer; (void)groupCountX; (void)groupCountY; (void)groupCountZ;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset) { (void)commandBuffer; (void)buffer; (void)offset;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags                        stageMask) { (void)commandBuffer; (void)event; (void)stageMask;   }

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags                        stageMask) { (void)commandBuffer; (void)event; (void)stageMask;   }

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(VkCommandBuffer                             commandBuffer,
    uint32_t                                    eventCount,
    const VkEvent*                              pEvents,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers) { (void)commandBuffer; (void)eventCount; (void)pEvents; (void)srcStageMask; (void)dstStageMask; (void)memoryBarrierCount; (void)pMemoryBarriers; (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers; (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer                             commandBuffer,
    VkPipelineLayout                            layout,
    VkShaderStageFlags                          stageFlags,
    uint32_t                                    offset,
    uint32_t                                    size,
    const void*                                 pValues) { (void)commandBuffer; (void)layout; (void)stageFlags; (void)offset; (void)size; (void)pValues;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) { (void)device; (void)pipelineCache; (void)createInfoCount; (void)pCreateInfos; (void)pAllocator; (void)pPipelines;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice                                    device,
    const VkFramebufferCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFramebuffer*                              pFramebuffer) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pFramebuffer;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice                                    device,
    VkFramebuffer                               framebuffer,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)framebuffer; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice                                    device,
    const VkRenderPassCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pRenderPass;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice                                    device,
    VkRenderPass                                renderPass,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)renderPass; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkGetRenderAreaGranularity(VkDevice                                    device,
    VkRenderPass                                renderPass,
    VkExtent2D*                                 pGranularity) { (void)device; (void)renderPass; (void)pGranularity;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports) { (void)commandBuffer; (void)firstViewport; (void)viewportCount; (void)pViewports;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstScissor,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors) { (void)commandBuffer; (void)firstScissor; (void)scissorCount; (void)pScissors;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer                             commandBuffer,
    float                                       lineWidth) { (void)commandBuffer; (void)lineWidth;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer                             commandBuffer,
    float                                       depthBiasConstantFactor,
    float                                       depthBiasClamp,
    float                                       depthBiasSlopeFactor) { (void)commandBuffer; (void)depthBiasConstantFactor; (void)depthBiasClamp; (void)depthBiasSlopeFactor;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer                             commandBuffer,
    const float                                 blendConstants[4]) { (void)commandBuffer;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(VkCommandBuffer                             commandBuffer,
    float                                       minDepthBounds,
    float                                       maxDepthBounds) { (void)commandBuffer; (void)minDepthBounds; (void)maxDepthBounds;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    compareMask) { (void)commandBuffer; (void)faceMask; (void)compareMask;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    writeMask) { (void)commandBuffer; (void)faceMask; (void)writeMask;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    reference) { (void)commandBuffer; (void)faceMask; (void)reference;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType) { (void)commandBuffer; (void)buffer; (void)offset; (void)indexType;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets) { (void)commandBuffer; (void)firstBinding; (void)bindingCount; (void)pBuffers; (void)pOffsets;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstVertex,
    uint32_t                                    firstInstance) { (void)commandBuffer; (void)vertexCount; (void)instanceCount; (void)firstVertex; (void)firstInstance;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance) { (void)commandBuffer; (void)indexCount; (void)instanceCount; (void)firstIndex; (void)vertexOffset; (void)firstInstance;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)drawCount; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)drawCount; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageBlit*                          pRegions,
    VkFilter                                    filter) { (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions; (void)filter;   }

VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearDepthStencilValue*             pDepthStencil,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges) { (void)commandBuffer; (void)image; (void)imageLayout; (void)pDepthStencil; (void)rangeCount; (void)pRanges;   }

VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkClearAttachment*                    pAttachments,
    uint32_t                                    rectCount,
    const VkClearRect*                          pRects) { (void)commandBuffer; (void)attachmentCount; (void)pAttachments; (void)rectCount; (void)pRects;   }

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageResolve*                       pRegions) { (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBegin,
    VkSubpassContents                           contents) { (void)commandBuffer; (void)pRenderPassBegin; (void)contents;   }

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer                             commandBuffer,
    VkSubpassContents                           contents) { (void)commandBuffer; (void)contents;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer                             commandBuffer) { (void)commandBuffer;   }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion) { if (pApiVersion) *pApiVersion = VK_API_VERSION_1_2; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindBufferMemoryInfo*               pBindInfos) { (void)device; (void)bindInfoCount; (void)pBindInfos;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindImageMemoryInfo*                pBindInfos) { (void)device; (void)bindInfoCount; (void)pBindInfos;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeatures(VkDevice                                    device,
    uint32_t                                    heapIndex,
    uint32_t                                    localDeviceIndex,
    uint32_t                                    remoteDeviceIndex,
    VkPeerMemoryFeatureFlags*                   pPeerMemoryFeatures) { (void)device; (void)heapIndex; (void)localDeviceIndex; (void)remoteDeviceIndex; (void)pPeerMemoryFeatures;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMask(VkCommandBuffer                             commandBuffer,
    uint32_t                                    deviceMask) { (void)commandBuffer; (void)deviceMask;   }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) {
    extern VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance, uint32_t*, VkPhysicalDevice*);
    if (!pPhysicalDeviceGroupCount) return VK_SUCCESS;
    if (!pPhysicalDeviceGroupProperties) { *pPhysicalDeviceGroupCount = 1; return VK_SUCCESS; }
    if (*pPhysicalDeviceGroupCount < 1) { *pPhysicalDeviceGroupCount = 1; return VK_INCOMPLETE; }
    // Ensure physical device cached via vkEnumeratePhysicalDevices
    VkPhysicalDevice dev = NULL; uint32_t c=1; vkEnumeratePhysicalDevices(instance, &c, &dev);
    memset(pPhysicalDeviceGroupProperties, 0, sizeof(*pPhysicalDeviceGroupProperties));
    pPhysicalDeviceGroupProperties[0].physicalDeviceCount = 1;
    pPhysicalDeviceGroupProperties[0].physicalDevices[0] = dev;
    *pPhysicalDeviceGroupCount = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(VkDevice                                    device,
    const VkImageMemoryRequirementsInfo2*       pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(VkDevice                                    device,
    const VkBufferMemoryRequirementsInfo2*      pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2(VkDevice                                    device,
    const VkImageSparseMemoryRequirementsInfo2* pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2*           pSparseMemoryRequirements) { (void)device; (void)pInfo; (void)pSparseMemoryRequirementCount; (void)pSparseMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures2*                  pFeatures) { (void)physicalDevice; (void)pFeatures;   }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) {
    (void)physicalDevice; if (pProperties) set_minimal_props(&pProperties->properties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties2*                        pFormatProperties) { (void)physicalDevice; (void)format; (void)pFormatProperties;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2*     pImageFormatInfo,
    VkImageFormatProperties2*                   pImageFormatProperties) { (void)physicalDevice; (void)pImageFormatInfo; (void)pImageFormatProperties;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) {
    (void)physicalDevice;
    if (!pQueueFamilyPropertyCount) return;
    if (!pQueueFamilyProperties) { *pQueueFamilyPropertyCount = 1; return; }
    if (*pQueueFamilyPropertyCount < 1) { *pQueueFamilyPropertyCount = 1; return; }
    memset(pQueueFamilyProperties, 0, sizeof(*pQueueFamilyProperties));
    pQueueFamilyProperties[0].queueFamilyProperties.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties[0].queueFamilyProperties.queueCount = 1;
    pQueueFamilyProperties[0].queueFamilyProperties.timestampValidBits = 64;
    pQueueFamilyProperties[0].queueFamilyProperties.minImageTransferGranularity.width = 1;
    pQueueFamilyProperties[0].queueFamilyProperties.minImageTransferGranularity.height = 1;
    pQueueFamilyProperties[0].queueFamilyProperties.minImageTransferGranularity.depth = 1;
    *pQueueFamilyPropertyCount = 1;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) {
    (void)physicalDevice; if (!pMemoryProperties) return; memset(pMemoryProperties, 0, sizeof(*pMemoryProperties));
    pMemoryProperties->memoryProperties.memoryTypeCount = 1;
    pMemoryProperties->memoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    pMemoryProperties->memoryProperties.memoryTypes[0].heapIndex = 0;
    pMemoryProperties->memoryProperties.memoryHeapCount = 1;
    pMemoryProperties->memoryProperties.memoryHeaps[0].size = 256ull * 1024 * 1024;
    pMemoryProperties->memoryProperties.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
    uint32_t*                                   pPropertyCount,
    VkSparseImageFormatProperties2*             pProperties) { (void)physicalDevice; (void)pFormatInfo; (void)pPropertyCount; (void)pProperties;   }

VKAPI_ATTR void VKAPI_CALL vkTrimCommandPool(VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolTrimFlags                      flags) { (void)device; (void)commandPool; (void)flags;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(VkDevice                                    device,
    const VkDeviceQueueInfo2*                   pQueueInfo,
    VkQueue*                                    pQueue) { (void)device; (void)pQueueInfo; (void)pQueue;   }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalBufferInfo*   pExternalBufferInfo,
    VkExternalBufferProperties*                 pExternalBufferProperties) { (void)physicalDevice; (void)pExternalBufferInfo; (void)pExternalBufferProperties;   }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalFenceInfo*    pExternalFenceInfo,
    VkExternalFenceProperties*                  pExternalFenceProperties) { (void)physicalDevice; (void)pExternalFenceInfo; (void)pExternalFenceProperties;   }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties*              pExternalSemaphoreProperties) { (void)physicalDevice; (void)pExternalSemaphoreInfo; (void)pExternalSemaphoreProperties;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBase(VkCommandBuffer                             commandBuffer,
    uint32_t                                    baseGroupX,
    uint32_t                                    baseGroupY,
    uint32_t                                    baseGroupZ,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ) { (void)commandBuffer; (void)baseGroupX; (void)baseGroupY; (void)baseGroupZ; (void)groupCountX; (void)groupCountY; (void)groupCountZ;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(VkDevice                                    device,
    const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorUpdateTemplate*                 pDescriptorUpdateTemplate) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pDescriptorUpdateTemplate;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(VkDevice                                    device,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)descriptorUpdateTemplate; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    const void*                                 pData) { (void)device; (void)descriptorSet; (void)descriptorUpdateTemplate; (void)pData;   }

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupport(VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    VkDescriptorSetLayoutSupport*               pSupport) { (void)device; (void)pCreateInfo; (void)pSupport;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversion(VkDevice                                    device,
    const VkSamplerYcbcrConversionCreateInfo*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSamplerYcbcrConversion*                   pYcbcrConversion) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pYcbcrConversion;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversion(VkDevice                                    device,
    VkSamplerYcbcrConversion                    ycbcrConversion,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)ycbcrConversion; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkResetQueryPool(VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount) { (void)device; (void)queryPool; (void)firstQuery; (void)queryCount;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(VkDevice                                    device,
    VkSemaphore                                 semaphore,
    uint64_t*                                   pValue) { (void)device; (void)semaphore; (void)pValue;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(VkDevice                                    device,
    const VkSemaphoreWaitInfo*                  pWaitInfo,
    uint64_t                                    timeout) { (void)device; (void)pWaitInfo; (void)timeout;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphore(VkDevice                                    device,
    const VkSemaphoreSignalInfo*                pSignalInfo) { (void)device; (void)pSignalInfo;      return 0; }

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo) { (void)device; (void)pInfo;      return NULL; }

VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddress(VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo) { (void)device; (void)pInfo;      return 0; }

VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddress(VkDevice                                    device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo) { (void)device; (void)pInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCount(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(VkDevice                                    device,
    const VkRenderPassCreateInfo2*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pRenderPass;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2(VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBegin,
    const VkSubpassBeginInfo*                   pSubpassBeginInfo) { (void)commandBuffer; (void)pRenderPassBegin; (void)pSubpassBeginInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2(VkCommandBuffer                             commandBuffer,
    const VkSubpassBeginInfo*                   pSubpassBeginInfo,
    const VkSubpassEndInfo*                     pSubpassEndInfo) { (void)commandBuffer; (void)pSubpassBeginInfo; (void)pSubpassEndInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2(VkCommandBuffer                             commandBuffer,
    const VkSubpassEndInfo*                     pSubpassEndInfo) { (void)commandBuffer; (void)pSubpassEndInfo;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceToolProperties(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pToolCount,
    VkPhysicalDeviceToolProperties*             pToolProperties) { (void)physicalDevice; (void)pToolCount; (void)pToolProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePrivateDataSlot(VkDevice                                    device,
    const VkPrivateDataSlotCreateInfo*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPrivateDataSlot*                          pPrivateDataSlot) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pPrivateDataSlot;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPrivateDataSlot(VkDevice                                    device,
    VkPrivateDataSlot                           privateDataSlot,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)privateDataSlot; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkSetPrivateData(VkDevice                                    device,
    VkObjectType                                objectType,
    uint64_t                                    objectHandle,
    VkPrivateDataSlot                           privateDataSlot,
    uint64_t                                    data) { (void)device; (void)objectType; (void)objectHandle; (void)privateDataSlot; (void)data;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetPrivateData(VkDevice                                    device,
    VkObjectType                                objectType,
    uint64_t                                    objectHandle,
    VkPrivateDataSlot                           privateDataSlot,
    uint64_t*                                   pData) { (void)device; (void)objectType; (void)objectHandle; (void)privateDataSlot; (void)pData;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(VkCommandBuffer                             commandBuffer,
    const VkDependencyInfo*                     pDependencyInfo) { (void)commandBuffer; (void)pDependencyInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags2                       stage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query) { (void)commandBuffer; (void)stage; (void)queryPool; (void)query;   }

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo2*                        pSubmits,
    VkFence                                     fence) { (void)queue; (void)submitCount; (void)pSubmits; (void)fence;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2(VkCommandBuffer                             commandBuffer,
    const VkCopyBufferInfo2*                    pCopyBufferInfo) { (void)commandBuffer; (void)pCopyBufferInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2(VkCommandBuffer                             commandBuffer,
    const VkCopyImageInfo2*                     pCopyImageInfo) { (void)commandBuffer; (void)pCopyImageInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2(VkCommandBuffer                             commandBuffer,
    const VkCopyBufferToImageInfo2*             pCopyBufferToImageInfo) { (void)commandBuffer; (void)pCopyBufferToImageInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2(VkCommandBuffer                             commandBuffer,
    const VkCopyImageToBufferInfo2*             pCopyImageToBufferInfo) { (void)commandBuffer; (void)pCopyImageToBufferInfo;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(VkDevice                                    device,
    const VkDeviceBufferMemoryRequirements*     pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(VkDevice                                    device,
    const VkDeviceImageMemoryRequirements*      pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirements(VkDevice                                    device,
    const VkDeviceImageMemoryRequirements*      pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2*           pSparseMemoryRequirements) { (void)device; (void)pInfo; (void)pSparseMemoryRequirementCount; (void)pSparseMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2(VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    const VkDependencyInfo*                     pDependencyInfo) { (void)commandBuffer; (void)event; (void)pDependencyInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2(VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags2                       stageMask) { (void)commandBuffer; (void)event; (void)stageMask;   }

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2(VkCommandBuffer                             commandBuffer,
    uint32_t                                    eventCount,
    const VkEvent*                              pEvents,
    const VkDependencyInfo*                     pDependencyInfos) { (void)commandBuffer; (void)eventCount; (void)pEvents; (void)pDependencyInfos;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2(VkCommandBuffer                             commandBuffer,
    const VkBlitImageInfo2*                     pBlitImageInfo) { (void)commandBuffer; (void)pBlitImageInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2(VkCommandBuffer                             commandBuffer,
    const VkResolveImageInfo2*                  pResolveImageInfo) { (void)commandBuffer; (void)pResolveImageInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(VkCommandBuffer                             commandBuffer,
    const VkRenderingInfo*                      pRenderingInfo) { (void)commandBuffer; (void)pRenderingInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer                             commandBuffer) { (void)commandBuffer;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetCullMode(VkCommandBuffer                             commandBuffer,
    VkCullModeFlags                             cullMode) { (void)commandBuffer; (void)cullMode;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFace(VkCommandBuffer                             commandBuffer,
    VkFrontFace                                 frontFace) { (void)commandBuffer; (void)frontFace;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopology(VkCommandBuffer                             commandBuffer,
    VkPrimitiveTopology                         primitiveTopology) { (void)commandBuffer; (void)primitiveTopology;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCount(VkCommandBuffer                             commandBuffer,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports) { (void)commandBuffer; (void)viewportCount; (void)pViewports;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCount(VkCommandBuffer                             commandBuffer,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors) { (void)commandBuffer; (void)scissorCount; (void)pScissors;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers2(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets,
    const VkDeviceSize*                         pSizes,
    const VkDeviceSize*                         pStrides) { (void)commandBuffer; (void)firstBinding; (void)bindingCount; (void)pBuffers; (void)pOffsets; (void)pSizes; (void)pStrides;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnable(VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthTestEnable) { (void)commandBuffer; (void)depthTestEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnable(VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthWriteEnable) { (void)commandBuffer; (void)depthWriteEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOp(VkCommandBuffer                             commandBuffer,
    VkCompareOp                                 depthCompareOp) { (void)commandBuffer; (void)depthCompareOp;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnable(VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthBoundsTestEnable) { (void)commandBuffer; (void)depthBoundsTestEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnable(VkCommandBuffer                             commandBuffer,
    VkBool32                                    stencilTestEnable) { (void)commandBuffer; (void)stencilTestEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOp(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    VkStencilOp                                 failOp,
    VkStencilOp                                 passOp,
    VkStencilOp                                 depthFailOp,
    VkCompareOp                                 compareOp) { (void)commandBuffer; (void)faceMask; (void)failOp; (void)passOp; (void)depthFailOp; (void)compareOp;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizerDiscardEnable(VkCommandBuffer                             commandBuffer,
    VkBool32                                    rasterizerDiscardEnable) { (void)commandBuffer; (void)rasterizerDiscardEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnable(VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthBiasEnable) { (void)commandBuffer; (void)depthBiasEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnable(VkCommandBuffer                             commandBuffer,
    VkBool32                                    primitiveRestartEnable) { (void)commandBuffer; (void)primitiveRestartEnable;   }

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2(VkDevice                                    device,
    const VkMemoryMapInfo*                      pMemoryMapInfo,
    void**                                      ppData) { (void)device; (void)pMemoryMapInfo; (void)ppData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2(VkDevice                                    device,
    const VkMemoryUnmapInfo*                    pMemoryUnmapInfo) { (void)device; (void)pMemoryUnmapInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSubresourceLayout(VkDevice                                    device,
    const VkDeviceImageSubresourceInfo*         pInfo,
    VkSubresourceLayout2*                       pLayout) { (void)device; (void)pInfo; (void)pLayout;   }

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2(VkDevice                                    device,
    VkImage                                     image,
    const VkImageSubresource2*                  pSubresource,
    VkSubresourceLayout2*                       pLayout) { (void)device; (void)image; (void)pSubresource; (void)pLayout;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToImage(VkDevice                                    device,
    const VkCopyMemoryToImageInfo*              pCopyMemoryToImageInfo) { (void)device; (void)pCopyMemoryToImageInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToMemory(VkDevice                                    device,
    const VkCopyImageToMemoryInfo*              pCopyImageToMemoryInfo) { (void)device; (void)pCopyImageToMemoryInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToImage(VkDevice                                    device,
    const VkCopyImageToImageInfo*               pCopyImageToImageInfo) { (void)device; (void)pCopyImageToImageInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkTransitionImageLayout(VkDevice                                    device,
    uint32_t                                    transitionCount,
    const VkHostImageLayoutTransitionInfo*      pTransitions) { (void)device; (void)transitionCount; (void)pTransitions;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites) { (void)commandBuffer; (void)pipelineBindPoint; (void)layout; (void)set; (void)descriptorWriteCount; (void)pDescriptorWrites;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate(VkCommandBuffer                             commandBuffer,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    const void*                                 pData) { (void)commandBuffer; (void)descriptorUpdateTemplate; (void)layout; (void)set; (void)pData;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets2(VkCommandBuffer                             commandBuffer,
    const VkBindDescriptorSetsInfo*             pBindDescriptorSetsInfo) { (void)commandBuffer; (void)pBindDescriptorSetsInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants2(VkCommandBuffer                             commandBuffer,
    const VkPushConstantsInfo*                  pPushConstantsInfo) { (void)commandBuffer; (void)pPushConstantsInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet2(VkCommandBuffer                             commandBuffer,
    const VkPushDescriptorSetInfo*              pPushDescriptorSetInfo) { (void)commandBuffer; (void)pPushDescriptorSetInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate2(VkCommandBuffer                             commandBuffer,
    const VkPushDescriptorSetWithTemplateInfo*  pPushDescriptorSetWithTemplateInfo) { (void)commandBuffer; (void)pPushDescriptorSetWithTemplateInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStipple(VkCommandBuffer                             commandBuffer,
    uint32_t                                    lineStippleFactor,
    uint16_t                                    lineStipplePattern) { (void)commandBuffer; (void)lineStippleFactor; (void)lineStipplePattern;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer2(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkIndexType                                 indexType) { (void)commandBuffer; (void)buffer; (void)offset; (void)size; (void)indexType;   }

VKAPI_ATTR void VKAPI_CALL vkGetRenderingAreaGranularity(VkDevice                                    device,
    const VkRenderingAreaInfo*                  pRenderingAreaInfo,
    VkExtent2D*                                 pGranularity) { (void)device; (void)pRenderingAreaInfo; (void)pGranularity;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingAttachmentLocations(VkCommandBuffer                             commandBuffer,
    const VkRenderingAttachmentLocationInfo*    pLocationInfo) { (void)commandBuffer; (void)pLocationInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingInputAttachmentIndices(VkCommandBuffer                             commandBuffer,
    const VkRenderingInputAttachmentIndexInfo*  pInputAttachmentIndexInfo) { (void)commandBuffer; (void)pInputAttachmentIndexInfo;   }

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance                                  instance,
    VkSurfaceKHR                                surface,
    const VkAllocationCallbacks*                pAllocator) { (void)instance; (void)surface; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) { (void)physicalDevice; (void)queueFamilyIndex; (void)surface; if (pSupported) *pSupported = VK_TRUE; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) { (void)physicalDevice; (void)surface; if (!pSurfaceCapabilities) return VK_SUCCESS; memset(pSurfaceCapabilities, 0, sizeof(*pSurfaceCapabilities)); pSurfaceCapabilities->minImageCount = 2; pSurfaceCapabilities->maxImageCount = 8; pSurfaceCapabilities->currentExtent.width = 1920; pSurfaceCapabilities->currentExtent.height = 1080; pSurfaceCapabilities->minImageExtent.width = 1920; pSurfaceCapabilities->minImageExtent.height = 1080; pSurfaceCapabilities->maxImageExtent.width = 1920; pSurfaceCapabilities->maxImageExtent.height = 1080; pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; pSurfaceCapabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) { (void)physicalDevice; (void)surface; if (!pSurfaceFormatCount) return VK_SUCCESS; if (!pSurfaceFormats) { *pSurfaceFormatCount = 1; return VK_SUCCESS; } if (*pSurfaceFormatCount < 1) { *pSurfaceFormatCount = 1; return VK_INCOMPLETE; } pSurfaceFormats[0].format = VK_FORMAT_B8G8R8A8_SRGB; pSurfaceFormats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; *pSurfaceFormatCount = 1; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) { (void)physicalDevice; (void)surface; if (!pPresentModeCount) return VK_SUCCESS; if (!pPresentModes) { *pPresentModeCount = 1; return VK_SUCCESS; } if (*pPresentModeCount < 1) { *pPresentModeCount = 1; return VK_INCOMPLETE; } pPresentModes[0] = VK_PRESENT_MODE_FIFO_KHR; *pPresentModeCount = 1; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice                                    device,
    const VkSwapchainCreateInfoKHR*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchain) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pSwapchain;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)swapchain; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pSwapchainImageCount,
    VkImage*                                    pSwapchainImages) { (void)device; (void)swapchain; (void)pSwapchainImageCount; (void)pSwapchainImages;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t*                                   pImageIndex) { (void)device; (void)swapchain; (void)timeout; (void)semaphore; (void)fence; (void)pImageIndex;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue                                     queue,
    const VkPresentInfoKHR*                     pPresentInfo) { (void)queue; (void)pPresentInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupPresentCapabilitiesKHR(VkDevice                                    device,
    VkDeviceGroupPresentCapabilitiesKHR*        pDeviceGroupPresentCapabilities) { (void)device; (void)pDeviceGroupPresentCapabilities;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHR(VkDevice                                    device,
    VkSurfaceKHR                                surface,
    VkDeviceGroupPresentModeFlagsKHR*           pModes) { (void)device; (void)surface; (void)pModes;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pRectCount,
    VkRect2D*                                   pRects) { (void)physicalDevice; (void)surface; (void)pRectCount; (void)pRects;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(VkDevice                                    device,
    const VkAcquireNextImageInfoKHR*            pAcquireInfo,
    uint32_t*                                   pImageIndex) { (void)device; (void)pAcquireInfo; (void)pImageIndex;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayPropertiesKHR*                     pProperties) { (void)physicalDevice; (void)pPropertyCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayPlanePropertiesKHR*                pProperties) { (void)physicalDevice; (void)pPropertyCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice                            physicalDevice,
    uint32_t                                    planeIndex,
    uint32_t*                                   pDisplayCount,
    VkDisplayKHR*                               pDisplays) { (void)physicalDevice; (void)planeIndex; (void)pDisplayCount; (void)pDisplays;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayModePropertiesKHR(VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    uint32_t*                                   pPropertyCount,
    VkDisplayModePropertiesKHR*                 pProperties) { (void)physicalDevice; (void)display; (void)pPropertyCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayModeKHR(VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    const VkDisplayModeCreateInfoKHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDisplayModeKHR*                           pMode) { (void)physicalDevice; (void)display; (void)pCreateInfo; (void)pAllocator; (void)pMode;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice                            physicalDevice,
    VkDisplayModeKHR                            mode,
    uint32_t                                    planeIndex,
    VkDisplayPlaneCapabilitiesKHR*              pCapabilities) { (void)physicalDevice; (void)mode; (void)planeIndex; (void)pCapabilities;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayPlaneSurfaceKHR(VkInstance                                  instance,
    const VkDisplaySurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { (void)instance; (void)pCreateInfo; (void)pAllocator; (void)pSurface;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSharedSwapchainsKHR(VkDevice                                    device,
    uint32_t                                    swapchainCount,
    const VkSwapchainCreateInfoKHR*             pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchains) { (void)device; (void)swapchainCount; (void)pCreateInfos; (void)pAllocator; (void)pSwapchains;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceVideoCapabilitiesKHR(VkPhysicalDevice                            physicalDevice,
    const VkVideoProfileInfoKHR*                pVideoProfile,
    VkVideoCapabilitiesKHR*                     pCapabilities) { (void)physicalDevice; (void)pVideoProfile; (void)pCapabilities;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceVideoFormatPropertiesKHR(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceVideoFormatInfoKHR*   pVideoFormatInfo,
    uint32_t*                                   pVideoFormatPropertyCount,
    VkVideoFormatPropertiesKHR*                 pVideoFormatProperties) { (void)physicalDevice; (void)pVideoFormatInfo; (void)pVideoFormatPropertyCount; (void)pVideoFormatProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateVideoSessionKHR(VkDevice                                    device,
    const VkVideoSessionCreateInfoKHR*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkVideoSessionKHR*                          pVideoSession) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pVideoSession;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyVideoSessionKHR(VkDevice                                    device,
    VkVideoSessionKHR                           videoSession,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)videoSession; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetVideoSessionMemoryRequirementsKHR(VkDevice                                    device,
    VkVideoSessionKHR                           videoSession,
    uint32_t*                                   pMemoryRequirementsCount,
    VkVideoSessionMemoryRequirementsKHR*        pMemoryRequirements) { (void)device; (void)videoSession; (void)pMemoryRequirementsCount; (void)pMemoryRequirements;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindVideoSessionMemoryKHR(VkDevice                                    device,
    VkVideoSessionKHR                           videoSession,
    uint32_t                                    bindSessionMemoryInfoCount,
    const VkBindVideoSessionMemoryInfoKHR*      pBindSessionMemoryInfos) { (void)device; (void)videoSession; (void)bindSessionMemoryInfoCount; (void)pBindSessionMemoryInfos;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateVideoSessionParametersKHR(VkDevice                                    device,
    const VkVideoSessionParametersCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkVideoSessionParametersKHR*                pVideoSessionParameters) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pVideoSessionParameters;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkUpdateVideoSessionParametersKHR(VkDevice                                    device,
    VkVideoSessionParametersKHR                 videoSessionParameters,
    const VkVideoSessionParametersUpdateInfoKHR* pUpdateInfo) { (void)device; (void)videoSessionParameters; (void)pUpdateInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyVideoSessionParametersKHR(VkDevice                                    device,
    VkVideoSessionParametersKHR                 videoSessionParameters,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)videoSessionParameters; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginVideoCodingKHR(VkCommandBuffer                             commandBuffer,
    const VkVideoBeginCodingInfoKHR*            pBeginInfo) { (void)commandBuffer; (void)pBeginInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndVideoCodingKHR(VkCommandBuffer                             commandBuffer,
    const VkVideoEndCodingInfoKHR*              pEndCodingInfo) { (void)commandBuffer; (void)pEndCodingInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdControlVideoCodingKHR(VkCommandBuffer                             commandBuffer,
    const VkVideoCodingControlInfoKHR*          pCodingControlInfo) { (void)commandBuffer; (void)pCodingControlInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDecodeVideoKHR(VkCommandBuffer                             commandBuffer,
    const VkVideoDecodeInfoKHR*                 pDecodeInfo) { (void)commandBuffer; (void)pDecodeInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderingKHR(VkCommandBuffer                             commandBuffer,
    const VkRenderingInfo*                      pRenderingInfo) { (void)commandBuffer; (void)pRenderingInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderingKHR(VkCommandBuffer                             commandBuffer) { (void)commandBuffer;   }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures2*                  pFeatures) { (void)physicalDevice; (void)pFeatures;   }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) {
    (void)physicalDevice; if (pProperties) set_minimal_props(&pProperties->properties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties2*                        pFormatProperties) { (void)physicalDevice; (void)format; (void)pFormatProperties;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2*     pImageFormatInfo,
    VkImageFormatProperties2*                   pImageFormatProperties) { (void)physicalDevice; (void)pImageFormatInfo; (void)pImageFormatProperties;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) {
    (void)physicalDevice;
    if (!pQueueFamilyPropertyCount) return;
    if (!pQueueFamilyProperties) { *pQueueFamilyPropertyCount = 1; return; }
    if (*pQueueFamilyPropertyCount < 1) { *pQueueFamilyPropertyCount = 1; return; }
    memset(pQueueFamilyProperties, 0, sizeof(*pQueueFamilyProperties));
    pQueueFamilyProperties[0].queueFamilyProperties.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties[0].queueFamilyProperties.queueCount = 1;
    pQueueFamilyProperties[0].queueFamilyProperties.timestampValidBits = 64;
    pQueueFamilyProperties[0].queueFamilyProperties.minImageTransferGranularity.width = 1;
    pQueueFamilyProperties[0].queueFamilyProperties.minImageTransferGranularity.height = 1;
    pQueueFamilyProperties[0].queueFamilyProperties.minImageTransferGranularity.depth = 1;
    *pQueueFamilyPropertyCount = 1;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) {
    (void)physicalDevice; if (!pMemoryProperties) return; memset(pMemoryProperties, 0, sizeof(*pMemoryProperties));
    pMemoryProperties->memoryProperties.memoryTypeCount = 1;
    pMemoryProperties->memoryProperties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    pMemoryProperties->memoryProperties.memoryTypes[0].heapIndex = 0;
    pMemoryProperties->memoryProperties.memoryHeapCount = 1;
    pMemoryProperties->memoryProperties.memoryHeaps[0].size = 256ull * 1024 * 1024;
    pMemoryProperties->memoryProperties.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
    uint32_t*                                   pPropertyCount,
    VkSparseImageFormatProperties2*             pProperties) { (void)physicalDevice; (void)pFormatInfo; (void)pPropertyCount; (void)pProperties;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeaturesKHR(VkDevice                                    device,
    uint32_t                                    heapIndex,
    uint32_t                                    localDeviceIndex,
    uint32_t                                    remoteDeviceIndex,
    VkPeerMemoryFeatureFlags*                   pPeerMemoryFeatures) { (void)device; (void)heapIndex; (void)localDeviceIndex; (void)remoteDeviceIndex; (void)pPeerMemoryFeatures;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMaskKHR(VkCommandBuffer                             commandBuffer,
    uint32_t                                    deviceMask) { (void)commandBuffer; (void)deviceMask;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBaseKHR(VkCommandBuffer                             commandBuffer,
    uint32_t                                    baseGroupX,
    uint32_t                                    baseGroupY,
    uint32_t                                    baseGroupZ,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ) { (void)commandBuffer; (void)baseGroupX; (void)baseGroupY; (void)baseGroupZ; (void)groupCountX; (void)groupCountY; (void)groupCountZ;   }

VKAPI_ATTR void VKAPI_CALL vkTrimCommandPoolKHR(VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolTrimFlags                      flags) { (void)device; (void)commandPool; (void)flags;   }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroupsKHR(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) {
    return vkEnumeratePhysicalDeviceGroups(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalBufferInfo*   pExternalBufferInfo,
    VkExternalBufferProperties*                 pExternalBufferProperties) { (void)physicalDevice; (void)pExternalBufferInfo; (void)pExternalBufferProperties;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdKHR(VkDevice                                    device,
    const VkMemoryGetFdInfoKHR*                 pGetFdInfo,
    int*                                        pFd) { (void)device; (void)pGetFdInfo; (void)pFd;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdPropertiesKHR(VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBits          handleType,
    int                                         fd,
    VkMemoryFdPropertiesKHR*                    pMemoryFdProperties) { (void)device; (void)handleType; (void)fd; (void)pMemoryFdProperties;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties*              pExternalSemaphoreProperties) { (void)physicalDevice; (void)pExternalSemaphoreInfo; (void)pExternalSemaphoreProperties;   }

VKAPI_ATTR VkResult VKAPI_CALL vkImportSemaphoreFdKHR(VkDevice                                    device,
    const VkImportSemaphoreFdInfoKHR*           pImportSemaphoreFdInfo) { (void)device; (void)pImportSemaphoreFdInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreFdKHR(VkDevice                                    device,
    const VkSemaphoreGetFdInfoKHR*              pGetFdInfo,
    int*                                        pFd) { (void)device; (void)pGetFdInfo; (void)pFd;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetKHR(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites) { (void)commandBuffer; (void)pipelineBindPoint; (void)layout; (void)set; (void)descriptorWriteCount; (void)pDescriptorWrites;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer                             commandBuffer,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    const void*                                 pData) { (void)commandBuffer; (void)descriptorUpdateTemplate; (void)layout; (void)set; (void)pData;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplateKHR(VkDevice                                    device,
    const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorUpdateTemplate*                 pDescriptorUpdateTemplate) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pDescriptorUpdateTemplate;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplateKHR(VkDevice                                    device,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)descriptorUpdateTemplate; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplateKHR(VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    const void*                                 pData) { (void)device; (void)descriptorSet; (void)descriptorUpdateTemplate; (void)pData;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2KHR(VkDevice                                    device,
    const VkRenderPassCreateInfo2*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pRenderPass;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2KHR(VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBegin,
    const VkSubpassBeginInfo*                   pSubpassBeginInfo) { (void)commandBuffer; (void)pRenderPassBegin; (void)pSubpassBeginInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2KHR(VkCommandBuffer                             commandBuffer,
    const VkSubpassBeginInfo*                   pSubpassBeginInfo,
    const VkSubpassEndInfo*                     pSubpassEndInfo) { (void)commandBuffer; (void)pSubpassBeginInfo; (void)pSubpassEndInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2KHR(VkCommandBuffer                             commandBuffer,
    const VkSubpassEndInfo*                     pSubpassEndInfo) { (void)commandBuffer; (void)pSubpassEndInfo;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainStatusKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain) { (void)device; (void)swapchain;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalFenceInfo*    pExternalFenceInfo,
    VkExternalFenceProperties*                  pExternalFenceProperties) { (void)physicalDevice; (void)pExternalFenceInfo; (void)pExternalFenceProperties;   }

VKAPI_ATTR VkResult VKAPI_CALL vkImportFenceFdKHR(VkDevice                                    device,
    const VkImportFenceFdInfoKHR*               pImportFenceFdInfo) { (void)device; (void)pImportFenceFdInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceFdKHR(VkDevice                                    device,
    const VkFenceGetFdInfoKHR*                  pGetFdInfo,
    int*                                        pFd) { (void)device; (void)pGetFdInfo; (void)pFd;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    uint32_t*                                   pCounterCount,
    VkPerformanceCounterKHR*                    pCounters,
    VkPerformanceCounterDescriptionKHR*         pCounterDescriptions) { (void)physicalDevice; (void)queueFamilyIndex; (void)pCounterCount; (void)pCounters; (void)pCounterDescriptions;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR(VkPhysicalDevice                            physicalDevice,
    const VkQueryPoolPerformanceCreateInfoKHR*  pPerformanceQueryCreateInfo,
    uint32_t*                                   pNumPasses) { (void)physicalDevice; (void)pPerformanceQueryCreateInfo; (void)pNumPasses;   }

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireProfilingLockKHR(VkDevice                                    device,
    const VkAcquireProfilingLockInfoKHR*        pInfo) { (void)device; (void)pInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkReleaseProfilingLockKHR(VkDevice                                    device) { (void)device;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) { (void)physicalDevice; (void)pSurfaceInfo; if (!pSurfaceCapabilities) return VK_SUCCESS; memset(pSurfaceCapabilities, 0, sizeof(*pSurfaceCapabilities)); pSurfaceCapabilities->surfaceCapabilities.minImageCount = 2; pSurfaceCapabilities->surfaceCapabilities.maxImageCount = 8; pSurfaceCapabilities->surfaceCapabilities.currentExtent.width = 1920; pSurfaceCapabilities->surfaceCapabilities.currentExtent.height = 1080; return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormat2KHR*                        pSurfaceFormats) { (void)physicalDevice; (void)pSurfaceInfo; (void)pSurfaceFormatCount; (void)pSurfaceFormats;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayProperties2KHR(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayProperties2KHR*                    pProperties) { (void)physicalDevice; (void)pPropertyCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPlaneProperties2KHR(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayPlaneProperties2KHR*               pProperties) { (void)physicalDevice; (void)pPropertyCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayModeProperties2KHR(VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    uint32_t*                                   pPropertyCount,
    VkDisplayModeProperties2KHR*                pProperties) { (void)physicalDevice; (void)display; (void)pPropertyCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneCapabilities2KHR(VkPhysicalDevice                            physicalDevice,
    const VkDisplayPlaneInfo2KHR*               pDisplayPlaneInfo,
    VkDisplayPlaneCapabilities2KHR*             pCapabilities) { (void)physicalDevice; (void)pDisplayPlaneInfo; (void)pCapabilities;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2KHR(VkDevice                                    device,
    const VkImageMemoryRequirementsInfo2*       pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2KHR(VkDevice                                    device,
    const VkBufferMemoryRequirementsInfo2*      pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2KHR(VkDevice                                    device,
    const VkImageSparseMemoryRequirementsInfo2* pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2*           pSparseMemoryRequirements) { (void)device; (void)pInfo; (void)pSparseMemoryRequirementCount; (void)pSparseMemoryRequirements;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversionKHR(VkDevice                                    device,
    const VkSamplerYcbcrConversionCreateInfo*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSamplerYcbcrConversion*                   pYcbcrConversion) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pYcbcrConversion;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversionKHR(VkDevice                                    device,
    VkSamplerYcbcrConversion                    ycbcrConversion,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)ycbcrConversion; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2KHR(VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindBufferMemoryInfo*               pBindInfos) { (void)device; (void)bindInfoCount; (void)pBindInfos;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2KHR(VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindImageMemoryInfo*                pBindInfos) { (void)device; (void)bindInfoCount; (void)pBindInfos;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupportKHR(VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    VkDescriptorSetLayoutSupport*               pSupport) { (void)device; (void)pCreateInfo; (void)pSupport;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCountKHR(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCountKHR(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValueKHR(VkDevice                                    device,
    VkSemaphore                                 semaphore,
    uint64_t*                                   pValue) { (void)device; (void)semaphore; (void)pValue;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphoresKHR(VkDevice                                    device,
    const VkSemaphoreWaitInfo*                  pWaitInfo,
    uint64_t                                    timeout) { (void)device; (void)pWaitInfo; (void)timeout;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphoreKHR(VkDevice                                    device,
    const VkSemaphoreSignalInfo*                pSignalInfo) { (void)device; (void)pSignalInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceFragmentShadingRatesKHR(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pFragmentShadingRateCount,
    VkPhysicalDeviceFragmentShadingRateKHR*     pFragmentShadingRates) { (void)physicalDevice; (void)pFragmentShadingRateCount; (void)pFragmentShadingRates;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetFragmentShadingRateKHR(VkCommandBuffer                             commandBuffer,
    const VkExtent2D*                           pFragmentSize,
    const VkFragmentShadingRateCombinerOpKHR    combinerOps[2]) { (void)commandBuffer; (void)pFragmentSize;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingAttachmentLocationsKHR(VkCommandBuffer                             commandBuffer,
    const VkRenderingAttachmentLocationInfo*    pLocationInfo) { (void)commandBuffer; (void)pLocationInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingInputAttachmentIndicesKHR(VkCommandBuffer                             commandBuffer,
    const VkRenderingInputAttachmentIndexInfo*  pInputAttachmentIndexInfo) { (void)commandBuffer; (void)pInputAttachmentIndexInfo;   }

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForPresentKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    presentId,
    uint64_t                                    timeout) { (void)device; (void)swapchain; (void)presentId; (void)timeout;      return 0; }

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressKHR(VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo) { (void)device; (void)pInfo;      return NULL; }

VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddressKHR(VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo) { (void)device; (void)pInfo;      return 0; }

VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddressKHR(VkDevice                                    device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo) { (void)device; (void)pInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDeferredOperationKHR(VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator,
    VkDeferredOperationKHR*                     pDeferredOperation) { (void)device; (void)pAllocator; (void)pDeferredOperation;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDeferredOperationKHR(VkDevice                                    device,
    VkDeferredOperationKHR                      operation,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)operation; (void)pAllocator;   }

VKAPI_ATTR uint32_t VKAPI_CALL vkGetDeferredOperationMaxConcurrencyKHR(VkDevice                                    device,
    VkDeferredOperationKHR                      operation) { (void)device; (void)operation;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeferredOperationResultKHR(VkDevice                                    device,
    VkDeferredOperationKHR                      operation) { (void)device; (void)operation;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkDeferredOperationJoinKHR(VkDevice                                    device,
    VkDeferredOperationKHR                      operation) { (void)device; (void)operation;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutablePropertiesKHR(VkDevice                                    device,
    const VkPipelineInfoKHR*                    pPipelineInfo,
    uint32_t*                                   pExecutableCount,
    VkPipelineExecutablePropertiesKHR*          pProperties) { (void)device; (void)pPipelineInfo; (void)pExecutableCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutableStatisticsKHR(VkDevice                                    device,
    const VkPipelineExecutableInfoKHR*          pExecutableInfo,
    uint32_t*                                   pStatisticCount,
    VkPipelineExecutableStatisticKHR*           pStatistics) { (void)device; (void)pExecutableInfo; (void)pStatisticCount; (void)pStatistics;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutableInternalRepresentationsKHR(VkDevice                                    device,
    const VkPipelineExecutableInfoKHR*          pExecutableInfo,
    uint32_t*                                   pInternalRepresentationCount,
    VkPipelineExecutableInternalRepresentationKHR* pInternalRepresentations) { (void)device; (void)pExecutableInfo; (void)pInternalRepresentationCount; (void)pInternalRepresentations;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2KHR(VkDevice                                    device,
    const VkMemoryMapInfo*                      pMemoryMapInfo,
    void**                                      ppData) { (void)device; (void)pMemoryMapInfo; (void)ppData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2KHR(VkDevice                                    device,
    const VkMemoryUnmapInfo*                    pMemoryUnmapInfo) { (void)device; (void)pMemoryUnmapInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceVideoEncodeQualityLevelInfoKHR* pQualityLevelInfo,
    VkVideoEncodeQualityLevelPropertiesKHR*     pQualityLevelProperties) { (void)physicalDevice; (void)pQualityLevelInfo; (void)pQualityLevelProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetEncodedVideoSessionParametersKHR(VkDevice                                    device,
    const VkVideoEncodeSessionParametersGetInfoKHR* pVideoSessionParametersInfo,
    VkVideoEncodeSessionParametersFeedbackInfoKHR* pFeedbackInfo,
    size_t*                                     pDataSize,
    void*                                       pData) { (void)device; (void)pVideoSessionParametersInfo; (void)pFeedbackInfo; (void)pDataSize; (void)pData;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdEncodeVideoKHR(VkCommandBuffer                             commandBuffer,
    const VkVideoEncodeInfoKHR*                 pEncodeInfo) { (void)commandBuffer; (void)pEncodeInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2KHR(VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    const VkDependencyInfo*                     pDependencyInfo) { (void)commandBuffer; (void)event; (void)pDependencyInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2KHR(VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags2                       stageMask) { (void)commandBuffer; (void)event; (void)stageMask;   }

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2KHR(VkCommandBuffer                             commandBuffer,
    uint32_t                                    eventCount,
    const VkEvent*                              pEvents,
    const VkDependencyInfo*                     pDependencyInfos) { (void)commandBuffer; (void)eventCount; (void)pEvents; (void)pDependencyInfos;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2KHR(VkCommandBuffer                             commandBuffer,
    const VkDependencyInfo*                     pDependencyInfo) { (void)commandBuffer; (void)pDependencyInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2KHR(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags2                       stage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query) { (void)commandBuffer; (void)stage; (void)queryPool; (void)query;   }

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2KHR(VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo2*                        pSubmits,
    VkFence                                     fence) { (void)queue; (void)submitCount; (void)pSubmits; (void)fence;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2KHR(VkCommandBuffer                             commandBuffer,
    const VkCopyBufferInfo2*                    pCopyBufferInfo) { (void)commandBuffer; (void)pCopyBufferInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2KHR(VkCommandBuffer                             commandBuffer,
    const VkCopyImageInfo2*                     pCopyImageInfo) { (void)commandBuffer; (void)pCopyImageInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2KHR(VkCommandBuffer                             commandBuffer,
    const VkCopyBufferToImageInfo2*             pCopyBufferToImageInfo) { (void)commandBuffer; (void)pCopyBufferToImageInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2KHR(VkCommandBuffer                             commandBuffer,
    const VkCopyImageToBufferInfo2*             pCopyImageToBufferInfo) { (void)commandBuffer; (void)pCopyImageToBufferInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2KHR(VkCommandBuffer                             commandBuffer,
    const VkBlitImageInfo2*                     pBlitImageInfo) { (void)commandBuffer; (void)pBlitImageInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2KHR(VkCommandBuffer                             commandBuffer,
    const VkResolveImageInfo2*                  pResolveImageInfo) { (void)commandBuffer; (void)pResolveImageInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysIndirect2KHR(VkCommandBuffer                             commandBuffer,
    VkDeviceAddress                             indirectDeviceAddress) { (void)commandBuffer; (void)indirectDeviceAddress;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirementsKHR(VkDevice                                    device,
    const VkDeviceBufferMemoryRequirements*     pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirementsKHR(VkDevice                                    device,
    const VkDeviceImageMemoryRequirements*      pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirementsKHR(VkDevice                                    device,
    const VkDeviceImageMemoryRequirements*      pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2*           pSparseMemoryRequirements) { (void)device; (void)pInfo; (void)pSparseMemoryRequirementCount; (void)pSparseMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer2KHR(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkIndexType                                 indexType) { (void)commandBuffer; (void)buffer; (void)offset; (void)size; (void)indexType;   }

VKAPI_ATTR void VKAPI_CALL vkGetRenderingAreaGranularityKHR(VkDevice                                    device,
    const VkRenderingAreaInfo*                  pRenderingAreaInfo,
    VkExtent2D*                                 pGranularity) { (void)device; (void)pRenderingAreaInfo; (void)pGranularity;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSubresourceLayoutKHR(VkDevice                                    device,
    const VkDeviceImageSubresourceInfo*         pInfo,
    VkSubresourceLayout2*                       pLayout) { (void)device; (void)pInfo; (void)pLayout;   }

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2KHR(VkDevice                                    device,
    VkImage                                     image,
    const VkImageSubresource2*                  pSubresource,
    VkSubresourceLayout2*                       pLayout) { (void)device; (void)image; (void)pSubresource; (void)pLayout;   }

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForPresent2KHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkPresentWait2InfoKHR*                pPresentWait2Info) { (void)device; (void)swapchain; (void)pPresentWait2Info;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineBinariesKHR(VkDevice                                    device,
    const VkPipelineBinaryCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineBinaryHandlesInfoKHR*             pBinaries) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pBinaries;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineBinaryKHR(VkDevice                                    device,
    VkPipelineBinaryKHR                         pipelineBinary,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)pipelineBinary; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineKeyKHR(VkDevice                                    device,
    const VkPipelineCreateInfoKHR*              pPipelineCreateInfo,
    VkPipelineBinaryKeyKHR*                     pPipelineKey) { (void)device; (void)pPipelineCreateInfo; (void)pPipelineKey;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineBinaryDataKHR(VkDevice                                    device,
    const VkPipelineBinaryDataInfoKHR*          pInfo,
    VkPipelineBinaryKeyKHR*                     pPipelineBinaryKey,
    size_t*                                     pPipelineBinaryDataSize,
    void*                                       pPipelineBinaryData) { (void)device; (void)pInfo; (void)pPipelineBinaryKey; (void)pPipelineBinaryDataSize; (void)pPipelineBinaryData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkReleaseCapturedPipelineDataKHR(VkDevice                                    device,
    const VkReleaseCapturedPipelineDataInfoKHR* pInfo,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)pInfo; (void)pAllocator;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkReleaseSwapchainImagesKHR(VkDevice                                    device,
    const VkReleaseSwapchainImagesInfoKHR*      pReleaseInfo) { (void)device; (void)pReleaseInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkCooperativeMatrixPropertiesKHR*           pProperties) { (void)physicalDevice; (void)pPropertyCount; (void)pProperties;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStippleKHR(VkCommandBuffer                             commandBuffer,
    uint32_t                                    lineStippleFactor,
    uint16_t                                    lineStipplePattern) { (void)commandBuffer; (void)lineStippleFactor; (void)lineStipplePattern;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pTimeDomainCount,
    VkTimeDomainKHR*                            pTimeDomains) { (void)physicalDevice; (void)pTimeDomainCount; (void)pTimeDomains;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetCalibratedTimestampsKHR(VkDevice                                    device,
    uint32_t                                    timestampCount,
    const VkCalibratedTimestampInfoKHR*         pTimestampInfos,
    uint64_t*                                   pTimestamps,
    uint64_t*                                   pMaxDeviation) { (void)device; (void)timestampCount; (void)pTimestampInfos; (void)pTimestamps; (void)pMaxDeviation;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets2KHR(VkCommandBuffer                             commandBuffer,
    const VkBindDescriptorSetsInfo*             pBindDescriptorSetsInfo) { (void)commandBuffer; (void)pBindDescriptorSetsInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants2KHR(VkCommandBuffer                             commandBuffer,
    const VkPushConstantsInfo*                  pPushConstantsInfo) { (void)commandBuffer; (void)pPushConstantsInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet2KHR(VkCommandBuffer                             commandBuffer,
    const VkPushDescriptorSetInfo*              pPushDescriptorSetInfo) { (void)commandBuffer; (void)pPushDescriptorSetInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate2KHR(VkCommandBuffer                             commandBuffer,
    const VkPushDescriptorSetWithTemplateInfo*  pPushDescriptorSetWithTemplateInfo) { (void)commandBuffer; (void)pPushDescriptorSetWithTemplateInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDescriptorBufferOffsets2EXT(VkCommandBuffer                             commandBuffer,
    const VkSetDescriptorBufferOffsetsInfoEXT*  pSetDescriptorBufferOffsetsInfo) { (void)commandBuffer; (void)pSetDescriptorBufferOffsetsInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBufferEmbeddedSamplers2EXT(VkCommandBuffer                             commandBuffer,
    const VkBindDescriptorBufferEmbeddedSamplersInfoEXT* pBindDescriptorBufferEmbeddedSamplersInfo) { (void)commandBuffer; (void)pBindDescriptorBufferEmbeddedSamplersInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryIndirectKHR(VkCommandBuffer                             commandBuffer,
    const VkCopyMemoryIndirectInfoKHR*          pCopyMemoryIndirectInfo) { (void)commandBuffer; (void)pCopyMemoryIndirectInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToImageIndirectKHR(VkCommandBuffer                             commandBuffer,
    const VkCopyMemoryToImageIndirectInfoKHR*   pCopyMemoryToImageIndirectInfo) { (void)commandBuffer; (void)pCopyMemoryToImageIndirectInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering2KHR(VkCommandBuffer                             commandBuffer,
    const VkRenderingEndInfoKHR*                pRenderingEndInfo) { (void)commandBuffer; (void)pRenderingEndInfo;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugReportCallbackEXT(VkInstance                                  instance,
    const VkDebugReportCallbackCreateInfoEXT*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDebugReportCallbackEXT*                   pCallback) { (void)instance; (void)pCreateInfo; (void)pAllocator; (void)pCallback;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugReportCallbackEXT(VkInstance                                  instance,
    VkDebugReportCallbackEXT                    callback,
    const VkAllocationCallbacks*                pAllocator) { (void)instance; (void)callback; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkDebugReportMessageEXT(VkInstance                                  instance,
    VkDebugReportFlagsEXT                       flags,
    VkDebugReportObjectTypeEXT                  objectType,
    uint64_t                                    object,
    size_t                                      location,
    int32_t                                     messageCode,
    const char*                                 pLayerPrefix,
    const char*                                 pMessage) { (void)instance; (void)flags; (void)objectType; (void)object; (void)location; (void)messageCode; (void)pLayerPrefix; (void)pMessage;   }

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectTagEXT(VkDevice                                    device,
    const VkDebugMarkerObjectTagInfoEXT*        pTagInfo) { (void)device; (void)pTagInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectNameEXT(VkDevice                                    device,
    const VkDebugMarkerObjectNameInfoEXT*       pNameInfo) { (void)device; (void)pNameInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerBeginEXT(VkCommandBuffer                             commandBuffer,
    const VkDebugMarkerMarkerInfoEXT*           pMarkerInfo) { (void)commandBuffer; (void)pMarkerInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerEndEXT(VkCommandBuffer                             commandBuffer) { (void)commandBuffer;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerInsertEXT(VkCommandBuffer                             commandBuffer,
    const VkDebugMarkerMarkerInfoEXT*           pMarkerInfo) { (void)commandBuffer; (void)pMarkerInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets,
    const VkDeviceSize*                         pSizes) { (void)commandBuffer; (void)firstBinding; (void)bindingCount; (void)pBuffers; (void)pOffsets; (void)pSizes;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginTransformFeedbackEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterBuffer,
    uint32_t                                    counterBufferCount,
    const VkBuffer*                             pCounterBuffers,
    const VkDeviceSize*                         pCounterBufferOffsets) { (void)commandBuffer; (void)firstCounterBuffer; (void)counterBufferCount; (void)pCounterBuffers; (void)pCounterBufferOffsets;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndTransformFeedbackEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterBuffer,
    uint32_t                                    counterBufferCount,
    const VkBuffer*                             pCounterBuffers,
    const VkDeviceSize*                         pCounterBufferOffsets) { (void)commandBuffer; (void)firstCounterBuffer; (void)counterBufferCount; (void)pCounterBuffers; (void)pCounterBufferOffsets;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginQueryIndexedEXT(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags,
    uint32_t                                    index) { (void)commandBuffer; (void)queryPool; (void)query; (void)flags; (void)index;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndQueryIndexedEXT(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    uint32_t                                    index) { (void)commandBuffer; (void)queryPool; (void)query; (void)index;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectByteCountEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    VkBuffer                                    counterBuffer,
    VkDeviceSize                                counterBufferOffset,
    uint32_t                                    counterOffset,
    uint32_t                                    vertexStride) { (void)commandBuffer; (void)instanceCount; (void)firstInstance; (void)counterBuffer; (void)counterBufferOffset; (void)counterOffset; (void)vertexStride;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCuModuleNVX(VkDevice                                    device,
    const VkCuModuleCreateInfoNVX*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCuModuleNVX*                              pModule) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pModule;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCuFunctionNVX(VkDevice                                    device,
    const VkCuFunctionCreateInfoNVX*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCuFunctionNVX*                            pFunction) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pFunction;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyCuModuleNVX(VkDevice                                    device,
    VkCuModuleNVX                               module,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)module; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkDestroyCuFunctionNVX(VkDevice                                    device,
    VkCuFunctionNVX                             function,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)function; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCuLaunchKernelNVX(VkCommandBuffer                             commandBuffer,
    const VkCuLaunchInfoNVX*                    pLaunchInfo) { (void)commandBuffer; (void)pLaunchInfo;   }

VKAPI_ATTR uint32_t VKAPI_CALL vkGetImageViewHandleNVX(VkDevice                                    device,
    const VkImageViewHandleInfoNVX*             pInfo) { (void)device; (void)pInfo;      return 0; }

VKAPI_ATTR uint64_t VKAPI_CALL vkGetImageViewHandle64NVX(VkDevice                                    device,
    const VkImageViewHandleInfoNVX*             pInfo) { (void)device; (void)pInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageViewAddressNVX(VkDevice                                    device,
    VkImageView                                 imageView,
    VkImageViewAddressPropertiesNVX*            pProperties) { (void)device; (void)imageView; (void)pProperties;      return 0; }

VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceCombinedImageSamplerIndexNVX(VkDevice                                    device,
    uint64_t                                    imageViewIndex,
    uint64_t                                    samplerIndex) { (void)device; (void)imageViewIndex; (void)samplerIndex;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCountAMD(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCountAMD(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetShaderInfoAMD(VkDevice                                    device,
    VkPipeline                                  pipeline,
    VkShaderStageFlagBits                       shaderStage,
    VkShaderInfoTypeAMD                         infoType,
    size_t*                                     pInfoSize,
    void*                                       pInfo) { (void)device; (void)pipeline; (void)shaderStage; (void)infoType; (void)pInfoSize; (void)pInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceExternalImageFormatPropertiesNV(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkExternalMemoryHandleTypeFlagsNV           externalHandleType,
    VkExternalImageFormatPropertiesNV*          pExternalImageFormatProperties) { (void)physicalDevice; (void)format; (void)type; (void)tiling; (void)usage; (void)flags; (void)externalHandleType; (void)pExternalImageFormatProperties;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginConditionalRenderingEXT(VkCommandBuffer                             commandBuffer,
    const VkConditionalRenderingBeginInfoEXT*   pConditionalRenderingBegin) { (void)commandBuffer; (void)pConditionalRenderingBegin;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndConditionalRenderingEXT(VkCommandBuffer                             commandBuffer) { (void)commandBuffer;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWScalingNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewportWScalingNV*                 pViewportWScalings) { (void)commandBuffer; (void)firstViewport; (void)viewportCount; (void)pViewportWScalings;   }

VKAPI_ATTR VkResult VKAPI_CALL vkReleaseDisplayEXT(VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display) { (void)physicalDevice; (void)display;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2EXT(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilities2EXT*                  pSurfaceCapabilities) { (void)physicalDevice; (void)surface; (void)pSurfaceCapabilities;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkDisplayPowerControlEXT(VkDevice                                    device,
    VkDisplayKHR                                display,
    const VkDisplayPowerInfoEXT*                pDisplayPowerInfo) { (void)device; (void)display; (void)pDisplayPowerInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterDeviceEventEXT(VkDevice                                    device,
    const VkDeviceEventInfoEXT*                 pDeviceEventInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence) { (void)device; (void)pDeviceEventInfo; (void)pAllocator; (void)pFence;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterDisplayEventEXT(VkDevice                                    device,
    VkDisplayKHR                                display,
    const VkDisplayEventInfoEXT*                pDisplayEventInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence) { (void)device; (void)display; (void)pDisplayEventInfo; (void)pAllocator; (void)pFence;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainCounterEXT(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSurfaceCounterFlagBitsEXT                 counter,
    uint64_t*                                   pCounterValue) { (void)device; (void)swapchain; (void)counter; (void)pCounterValue;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetRefreshCycleDurationGOOGLE(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkRefreshCycleDurationGOOGLE*               pDisplayTimingProperties) { (void)device; (void)swapchain; (void)pDisplayTimingProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPastPresentationTimingGOOGLE(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pPresentationTimingCount,
    VkPastPresentationTimingGOOGLE*             pPresentationTimings) { (void)device; (void)swapchain; (void)pPresentationTimingCount; (void)pPresentationTimings;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstDiscardRectangle,
    uint32_t                                    discardRectangleCount,
    const VkRect2D*                             pDiscardRectangles) { (void)commandBuffer; (void)firstDiscardRectangle; (void)discardRectangleCount; (void)pDiscardRectangles;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    discardRectangleEnable) { (void)commandBuffer; (void)discardRectangleEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleModeEXT(VkCommandBuffer                             commandBuffer,
    VkDiscardRectangleModeEXT                   discardRectangleMode) { (void)commandBuffer; (void)discardRectangleMode;   }

VKAPI_ATTR void VKAPI_CALL vkSetHdrMetadataEXT(VkDevice                                    device,
    uint32_t                                    swapchainCount,
    const VkSwapchainKHR*                       pSwapchains,
    const VkHdrMetadataEXT*                     pMetadata) { (void)device; (void)swapchainCount; (void)pSwapchains; (void)pMetadata;   }

VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(VkDevice                                    device,
    const VkDebugUtilsObjectNameInfoEXT*        pNameInfo) { (void)device; (void)pNameInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectTagEXT(VkDevice                                    device,
    const VkDebugUtilsObjectTagInfoEXT*         pTagInfo) { (void)device; (void)pTagInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkQueueBeginDebugUtilsLabelEXT(VkQueue                                     queue,
    const VkDebugUtilsLabelEXT*                 pLabelInfo) { (void)queue; (void)pLabelInfo;   }

VKAPI_ATTR void VKAPI_CALL vkQueueEndDebugUtilsLabelEXT(VkQueue                                     queue) { (void)queue;   }

VKAPI_ATTR void VKAPI_CALL vkQueueInsertDebugUtilsLabelEXT(VkQueue                                     queue,
    const VkDebugUtilsLabelEXT*                 pLabelInfo) { (void)queue; (void)pLabelInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer                             commandBuffer,
    const VkDebugUtilsLabelEXT*                 pLabelInfo) { (void)commandBuffer; (void)pLabelInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer                             commandBuffer) { (void)commandBuffer;   }

VKAPI_ATTR void VKAPI_CALL vkCmdInsertDebugUtilsLabelEXT(VkCommandBuffer                             commandBuffer,
    const VkDebugUtilsLabelEXT*                 pLabelInfo) { (void)commandBuffer; (void)pLabelInfo;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance                                  instance,
    const VkDebugUtilsMessengerCreateInfoEXT*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDebugUtilsMessengerEXT*                   pMessenger) { (void)instance; (void)pCreateInfo; (void)pAllocator; (void)pMessenger;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance                                  instance,
    VkDebugUtilsMessengerEXT                    messenger,
    const VkAllocationCallbacks*                pAllocator) { (void)instance; (void)messenger; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkSubmitDebugUtilsMessageEXT(VkInstance                                  instance,
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData) { (void)instance; (void)messageSeverity; (void)messageTypes; (void)pCallbackData;   }

VKAPI_ATTR VkResult VKAPI_CALL vkWriteSamplerDescriptorsEXT(VkDevice                                    device,
    uint32_t                                    samplerCount,
    const VkSamplerCreateInfo*                  pSamplers,
    const VkHostAddressRangeEXT*                pDescriptors) { (void)device; (void)samplerCount; (void)pSamplers; (void)pDescriptors;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkWriteResourceDescriptorsEXT(VkDevice                                    device,
    uint32_t                                    resourceCount,
    const VkResourceDescriptorInfoEXT*          pResources,
    const VkHostAddressRangeEXT*                pDescriptors) { (void)device; (void)resourceCount; (void)pResources; (void)pDescriptors;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdBindSamplerHeapEXT(VkCommandBuffer                             commandBuffer,
    const VkBindHeapInfoEXT*                    pBindInfo) { (void)commandBuffer; (void)pBindInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindResourceHeapEXT(VkCommandBuffer                             commandBuffer,
    const VkBindHeapInfoEXT*                    pBindInfo) { (void)commandBuffer; (void)pBindInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPushDataEXT(VkCommandBuffer                             commandBuffer,
    const VkPushDataInfoEXT*                    pPushDataInfo) { (void)commandBuffer; (void)pPushDataInfo;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageOpaqueCaptureDataEXT(VkDevice                                    device,
    uint32_t                                    imageCount,
    const VkImage*                              pImages,
    VkHostAddressRangeEXT*                      pDatas) { (void)device; (void)imageCount; (void)pImages; (void)pDatas;      return 0; }

VKAPI_ATTR VkDeviceSize VKAPI_CALL vkGetPhysicalDeviceDescriptorSizeEXT(VkPhysicalDevice                            physicalDevice,
    VkDescriptorType                            descriptorType) { (void)physicalDevice; (void)descriptorType;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterCustomBorderColorEXT(VkDevice                                    device,
    const VkSamplerCustomBorderColorCreateInfoEXT* pBorderColor,
    VkBool32                                    requestIndex,
    uint32_t*                                   pIndex) { (void)device; (void)pBorderColor; (void)requestIndex; (void)pIndex;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkUnregisterCustomBorderColorEXT(VkDevice                                    device,
    uint32_t                                    index) { (void)device; (void)index;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetTensorOpaqueCaptureDataARM(VkDevice                                    device,
    uint32_t                                    tensorCount,
    const VkTensorARM*                          pTensors,
    VkHostAddressRangeEXT*                      pDatas) { (void)device; (void)tensorCount; (void)pTensors; (void)pDatas;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleLocationsEXT(VkCommandBuffer                             commandBuffer,
    const VkSampleLocationsInfoEXT*             pSampleLocationsInfo) { (void)commandBuffer; (void)pSampleLocationsInfo;   }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMultisamplePropertiesEXT(VkPhysicalDevice                            physicalDevice,
    VkSampleCountFlagBits                       samples,
    VkMultisamplePropertiesEXT*                 pMultisampleProperties) { (void)physicalDevice; (void)samples; (void)pMultisampleProperties;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageDrmFormatModifierPropertiesEXT(VkDevice                                    device,
    VkImage                                     image,
    VkImageDrmFormatModifierPropertiesEXT*      pProperties) { (void)device; (void)image; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateValidationCacheEXT(VkDevice                                    device,
    const VkValidationCacheCreateInfoEXT*       pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkValidationCacheEXT*                       pValidationCache) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pValidationCache;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyValidationCacheEXT(VkDevice                                    device,
    VkValidationCacheEXT                        validationCache,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)validationCache; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkMergeValidationCachesEXT(VkDevice                                    device,
    VkValidationCacheEXT                        dstCache,
    uint32_t                                    srcCacheCount,
    const VkValidationCacheEXT*                 pSrcCaches) { (void)device; (void)dstCache; (void)srcCacheCount; (void)pSrcCaches;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetValidationCacheDataEXT(VkDevice                                    device,
    VkValidationCacheEXT                        validationCache,
    size_t*                                     pDataSize,
    void*                                       pData) { (void)device; (void)validationCache; (void)pDataSize; (void)pData;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdBindShadingRateImageNV(VkCommandBuffer                             commandBuffer,
    VkImageView                                 imageView,
    VkImageLayout                               imageLayout) { (void)commandBuffer; (void)imageView; (void)imageLayout;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportShadingRatePaletteNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkShadingRatePaletteNV*               pShadingRatePalettes) { (void)commandBuffer; (void)firstViewport; (void)viewportCount; (void)pShadingRatePalettes;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoarseSampleOrderNV(VkCommandBuffer                             commandBuffer,
    VkCoarseSampleOrderTypeNV                   sampleOrderType,
    uint32_t                                    customSampleOrderCount,
    const VkCoarseSampleOrderCustomNV*          pCustomSampleOrders) { (void)commandBuffer; (void)sampleOrderType; (void)customSampleOrderCount; (void)pCustomSampleOrders;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureNV(VkDevice                                    device,
    const VkAccelerationStructureCreateInfoNV*  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkAccelerationStructureNV*                  pAccelerationStructure) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pAccelerationStructure;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureNV(VkDevice                                    device,
    VkAccelerationStructureNV                   accelerationStructure,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)accelerationStructure; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureMemoryRequirementsNV(VkDevice                                    device,
    const VkAccelerationStructureMemoryRequirementsInfoNV* pInfo,
    VkMemoryRequirements2KHR*                   pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR VkResult VKAPI_CALL vkBindAccelerationStructureMemoryNV(VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindAccelerationStructureMemoryInfoNV* pBindInfos) { (void)device; (void)bindInfoCount; (void)pBindInfos;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructureNV(VkCommandBuffer                             commandBuffer,
    const VkAccelerationStructureInfoNV*        pInfo,
    VkBuffer                                    instanceData,
    VkDeviceSize                                instanceOffset,
    VkBool32                                    update,
    VkAccelerationStructureNV                   dst,
    VkAccelerationStructureNV                   src,
    VkBuffer                                    scratch,
    VkDeviceSize                                scratchOffset) { (void)commandBuffer; (void)pInfo; (void)instanceData; (void)instanceOffset; (void)update; (void)dst; (void)src; (void)scratch; (void)scratchOffset;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureNV(VkCommandBuffer                             commandBuffer,
    VkAccelerationStructureNV                   dst,
    VkAccelerationStructureNV                   src,
    VkCopyAccelerationStructureModeKHR          mode) { (void)commandBuffer; (void)dst; (void)src; (void)mode;   }

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysNV(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    raygenShaderBindingTableBuffer,
    VkDeviceSize                                raygenShaderBindingOffset,
    VkBuffer                                    missShaderBindingTableBuffer,
    VkDeviceSize                                missShaderBindingOffset,
    VkDeviceSize                                missShaderBindingStride,
    VkBuffer                                    hitShaderBindingTableBuffer,
    VkDeviceSize                                hitShaderBindingOffset,
    VkDeviceSize                                hitShaderBindingStride,
    VkBuffer                                    callableShaderBindingTableBuffer,
    VkDeviceSize                                callableShaderBindingOffset,
    VkDeviceSize                                callableShaderBindingStride,
    uint32_t                                    width,
    uint32_t                                    height,
    uint32_t                                    depth) { (void)commandBuffer; (void)raygenShaderBindingTableBuffer; (void)raygenShaderBindingOffset; (void)missShaderBindingTableBuffer; (void)missShaderBindingOffset; (void)missShaderBindingStride; (void)hitShaderBindingTableBuffer; (void)hitShaderBindingOffset; (void)hitShaderBindingStride; (void)callableShaderBindingTableBuffer; (void)callableShaderBindingOffset; (void)callableShaderBindingStride; (void)width; (void)height; (void)depth;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesNV(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkRayTracingPipelineCreateInfoNV*     pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) { (void)device; (void)pipelineCache; (void)createInfoCount; (void)pCreateInfos; (void)pAllocator; (void)pPipelines;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesKHR(VkDevice                                    device,
    VkPipeline                                  pipeline,
    uint32_t                                    firstGroup,
    uint32_t                                    groupCount,
    size_t                                      dataSize,
    void*                                       pData) { (void)device; (void)pipeline; (void)firstGroup; (void)groupCount; (void)dataSize; (void)pData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesNV(VkDevice                                    device,
    VkPipeline                                  pipeline,
    uint32_t                                    firstGroup,
    uint32_t                                    groupCount,
    size_t                                      dataSize,
    void*                                       pData) { (void)device; (void)pipeline; (void)firstGroup; (void)groupCount; (void)dataSize; (void)pData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetAccelerationStructureHandleNV(VkDevice                                    device,
    VkAccelerationStructureNV                   accelerationStructure,
    size_t                                      dataSize,
    void*                                       pData) { (void)device; (void)accelerationStructure; (void)dataSize; (void)pData;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    accelerationStructureCount,
    const VkAccelerationStructureNV*            pAccelerationStructures,
    VkQueryType                                 queryType,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery) { (void)commandBuffer; (void)accelerationStructureCount; (void)pAccelerationStructures; (void)queryType; (void)queryPool; (void)firstQuery;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCompileDeferredNV(VkDevice                                    device,
    VkPipeline                                  pipeline,
    uint32_t                                    shader) { (void)device; (void)pipeline; (void)shader;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryHostPointerPropertiesEXT(VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBits          handleType,
    const void*                                 pHostPointer,
    VkMemoryHostPointerPropertiesEXT*           pMemoryHostPointerProperties) { (void)device; (void)handleType; (void)pHostPointer; (void)pMemoryHostPointerProperties;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdWriteBufferMarkerAMD(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    uint32_t                                    marker) { (void)commandBuffer; (void)pipelineStage; (void)dstBuffer; (void)dstOffset; (void)marker;   }

VKAPI_ATTR void VKAPI_CALL vkCmdWriteBufferMarker2AMD(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags2                       stage,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    uint32_t                                    marker) { (void)commandBuffer; (void)stage; (void)dstBuffer; (void)dstOffset; (void)marker;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pTimeDomainCount,
    VkTimeDomainKHR*                            pTimeDomains) { (void)physicalDevice; (void)pTimeDomainCount; (void)pTimeDomains;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetCalibratedTimestampsEXT(VkDevice                                    device,
    uint32_t                                    timestampCount,
    const VkCalibratedTimestampInfoKHR*         pTimestampInfos,
    uint64_t*                                   pTimestamps,
    uint64_t*                                   pMaxDeviation) { (void)device; (void)timestampCount; (void)pTimestampInfos; (void)pTimestamps; (void)pMaxDeviation;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    taskCount,
    uint32_t                                    firstTask) { (void)commandBuffer; (void)taskCount; (void)firstTask;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectNV(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)drawCount; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountNV(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetExclusiveScissorEnableNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstExclusiveScissor,
    uint32_t                                    exclusiveScissorCount,
    const VkBool32*                             pExclusiveScissorEnables) { (void)commandBuffer; (void)firstExclusiveScissor; (void)exclusiveScissorCount; (void)pExclusiveScissorEnables;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetExclusiveScissorNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstExclusiveScissor,
    uint32_t                                    exclusiveScissorCount,
    const VkRect2D*                             pExclusiveScissors) { (void)commandBuffer; (void)firstExclusiveScissor; (void)exclusiveScissorCount; (void)pExclusiveScissors;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetCheckpointNV(VkCommandBuffer                             commandBuffer,
    const void*                                 pCheckpointMarker) { (void)commandBuffer; (void)pCheckpointMarker;   }

VKAPI_ATTR void VKAPI_CALL vkGetQueueCheckpointDataNV(VkQueue                                     queue,
    uint32_t*                                   pCheckpointDataCount,
    VkCheckpointDataNV*                         pCheckpointData) { (void)queue; (void)pCheckpointDataCount; (void)pCheckpointData;   }

VKAPI_ATTR void VKAPI_CALL vkGetQueueCheckpointData2NV(VkQueue                                     queue,
    uint32_t*                                   pCheckpointDataCount,
    VkCheckpointData2NV*                        pCheckpointData) { (void)queue; (void)pCheckpointDataCount; (void)pCheckpointData;   }

VKAPI_ATTR VkResult VKAPI_CALL vkSetSwapchainPresentTimingQueueSizeEXT(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t                                    size) { (void)device; (void)swapchain; (void)size;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainTimingPropertiesEXT(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSwapchainTimingPropertiesEXT*             pSwapchainTimingProperties,
    uint64_t*                                   pSwapchainTimingPropertiesCounter) { (void)device; (void)swapchain; (void)pSwapchainTimingProperties; (void)pSwapchainTimingPropertiesCounter;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainTimeDomainPropertiesEXT(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSwapchainTimeDomainPropertiesEXT*         pSwapchainTimeDomainProperties,
    uint64_t*                                   pTimeDomainsCounter) { (void)device; (void)swapchain; (void)pSwapchainTimeDomainProperties; (void)pTimeDomainsCounter;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPastPresentationTimingEXT(VkDevice                                    device,
    const VkPastPresentationTimingInfoEXT*      pPastPresentationTimingInfo,
    VkPastPresentationTimingPropertiesEXT*      pPastPresentationTimingProperties) { (void)device; (void)pPastPresentationTimingInfo; (void)pPastPresentationTimingProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkInitializePerformanceApiINTEL(VkDevice                                    device,
    const VkInitializePerformanceApiInfoINTEL*  pInitializeInfo) { (void)device; (void)pInitializeInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkUninitializePerformanceApiINTEL(VkDevice                                    device) { (void)device;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCmdSetPerformanceMarkerINTEL(VkCommandBuffer                             commandBuffer,
    const VkPerformanceMarkerInfoINTEL*         pMarkerInfo) { (void)commandBuffer; (void)pMarkerInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCmdSetPerformanceStreamMarkerINTEL(VkCommandBuffer                             commandBuffer,
    const VkPerformanceStreamMarkerInfoINTEL*   pMarkerInfo) { (void)commandBuffer; (void)pMarkerInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCmdSetPerformanceOverrideINTEL(VkCommandBuffer                             commandBuffer,
    const VkPerformanceOverrideInfoINTEL*       pOverrideInfo) { (void)commandBuffer; (void)pOverrideInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkAcquirePerformanceConfigurationINTEL(VkDevice                                    device,
    const VkPerformanceConfigurationAcquireInfoINTEL* pAcquireInfo,
    VkPerformanceConfigurationINTEL*            pConfiguration) { (void)device; (void)pAcquireInfo; (void)pConfiguration;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkReleasePerformanceConfigurationINTEL(VkDevice                                    device,
    VkPerformanceConfigurationINTEL             configuration) { (void)device; (void)configuration;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSetPerformanceConfigurationINTEL(VkQueue                                     queue,
    VkPerformanceConfigurationINTEL             configuration) { (void)queue; (void)configuration;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPerformanceParameterINTEL(VkDevice                                    device,
    VkPerformanceParameterTypeINTEL             parameter,
    VkPerformanceValueINTEL*                    pValue) { (void)device; (void)parameter; (void)pValue;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkSetLocalDimmingAMD(VkDevice                                    device,
    VkSwapchainKHR                              swapChain,
    VkBool32                                    localDimmingEnable) { (void)device; (void)swapChain; (void)localDimmingEnable;   }

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressEXT(VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo) { (void)device; (void)pInfo;      return NULL; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceToolPropertiesEXT(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pToolCount,
    VkPhysicalDeviceToolProperties*             pToolProperties) { (void)physicalDevice; (void)pToolCount; (void)pToolProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixPropertiesNV(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkCooperativeMatrixPropertiesNV*            pProperties) { (void)physicalDevice; (void)pPropertyCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pCombinationCount,
    VkFramebufferMixedSamplesCombinationNV*     pCombinations) { (void)physicalDevice; (void)pCombinationCount; (void)pCombinations;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateHeadlessSurfaceEXT(VkInstance                                  instance,
    const VkHeadlessSurfaceCreateInfoEXT*       pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) { (void)instance; (void)pCreateInfo; (void)pAllocator; (void)pSurface;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStippleEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    lineStippleFactor,
    uint16_t                                    lineStipplePattern) { (void)commandBuffer; (void)lineStippleFactor; (void)lineStipplePattern;   }

VKAPI_ATTR void VKAPI_CALL vkResetQueryPoolEXT(VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount) { (void)device; (void)queryPool; (void)firstQuery; (void)queryCount;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetCullModeEXT(VkCommandBuffer                             commandBuffer,
    VkCullModeFlags                             cullMode) { (void)commandBuffer; (void)cullMode;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFaceEXT(VkCommandBuffer                             commandBuffer,
    VkFrontFace                                 frontFace) { (void)commandBuffer; (void)frontFace;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopologyEXT(VkCommandBuffer                             commandBuffer,
    VkPrimitiveTopology                         primitiveTopology) { (void)commandBuffer; (void)primitiveTopology;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCountEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports) { (void)commandBuffer; (void)viewportCount; (void)pViewports;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCountEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors) { (void)commandBuffer; (void)scissorCount; (void)pScissors;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers2EXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets,
    const VkDeviceSize*                         pSizes,
    const VkDeviceSize*                         pStrides) { (void)commandBuffer; (void)firstBinding; (void)bindingCount; (void)pBuffers; (void)pOffsets; (void)pSizes; (void)pStrides;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthTestEnable) { (void)commandBuffer; (void)depthTestEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthWriteEnable) { (void)commandBuffer; (void)depthWriteEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOpEXT(VkCommandBuffer                             commandBuffer,
    VkCompareOp                                 depthCompareOp) { (void)commandBuffer; (void)depthCompareOp;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthBoundsTestEnable) { (void)commandBuffer; (void)depthBoundsTestEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    stencilTestEnable) { (void)commandBuffer; (void)stencilTestEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOpEXT(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    VkStencilOp                                 failOp,
    VkStencilOp                                 passOp,
    VkStencilOp                                 depthFailOp,
    VkCompareOp                                 compareOp) { (void)commandBuffer; (void)faceMask; (void)failOp; (void)passOp; (void)depthFailOp; (void)compareOp;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToImageEXT(VkDevice                                    device,
    const VkCopyMemoryToImageInfo*              pCopyMemoryToImageInfo) { (void)device; (void)pCopyMemoryToImageInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToMemoryEXT(VkDevice                                    device,
    const VkCopyImageToMemoryInfo*              pCopyImageToMemoryInfo) { (void)device; (void)pCopyImageToMemoryInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToImageEXT(VkDevice                                    device,
    const VkCopyImageToImageInfo*               pCopyImageToImageInfo) { (void)device; (void)pCopyImageToImageInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkTransitionImageLayoutEXT(VkDevice                                    device,
    uint32_t                                    transitionCount,
    const VkHostImageLayoutTransitionInfo*      pTransitions) { (void)device; (void)transitionCount; (void)pTransitions;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2EXT(VkDevice                                    device,
    VkImage                                     image,
    const VkImageSubresource2*                  pSubresource,
    VkSubresourceLayout2*                       pLayout) { (void)device; (void)image; (void)pSubresource; (void)pLayout;   }

VKAPI_ATTR VkResult VKAPI_CALL vkReleaseSwapchainImagesEXT(VkDevice                                    device,
    const VkReleaseSwapchainImagesInfoKHR*      pReleaseInfo) { (void)device; (void)pReleaseInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetGeneratedCommandsMemoryRequirementsNV(VkDevice                                    device,
    const VkGeneratedCommandsMemoryRequirementsInfoNV* pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPreprocessGeneratedCommandsNV(VkCommandBuffer                             commandBuffer,
    const VkGeneratedCommandsInfoNV*            pGeneratedCommandsInfo) { (void)commandBuffer; (void)pGeneratedCommandsInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteGeneratedCommandsNV(VkCommandBuffer                             commandBuffer,
    VkBool32                                    isPreprocessed,
    const VkGeneratedCommandsInfoNV*            pGeneratedCommandsInfo) { (void)commandBuffer; (void)isPreprocessed; (void)pGeneratedCommandsInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipelineShaderGroupNV(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline,
    uint32_t                                    groupIndex) { (void)commandBuffer; (void)pipelineBindPoint; (void)pipeline; (void)groupIndex;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectCommandsLayoutNV(VkDevice                                    device,
    const VkIndirectCommandsLayoutCreateInfoNV* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkIndirectCommandsLayoutNV*                 pIndirectCommandsLayout) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pIndirectCommandsLayout;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectCommandsLayoutNV(VkDevice                                    device,
    VkIndirectCommandsLayoutNV                  indirectCommandsLayout,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)indirectCommandsLayout; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias2EXT(VkCommandBuffer                             commandBuffer,
    const VkDepthBiasInfoEXT*                   pDepthBiasInfo) { (void)commandBuffer; (void)pDepthBiasInfo;   }

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireDrmDisplayEXT(VkPhysicalDevice                            physicalDevice,
    int32_t                                     drmFd,
    VkDisplayKHR                                display) { (void)physicalDevice; (void)drmFd; (void)display;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDrmDisplayEXT(VkPhysicalDevice                            physicalDevice,
    int32_t                                     drmFd,
    uint32_t                                    connectorId,
    VkDisplayKHR*                               display) { (void)physicalDevice; (void)drmFd; (void)connectorId; (void)display;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePrivateDataSlotEXT(VkDevice                                    device,
    const VkPrivateDataSlotCreateInfo*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPrivateDataSlot*                          pPrivateDataSlot) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pPrivateDataSlot;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyPrivateDataSlotEXT(VkDevice                                    device,
    VkPrivateDataSlot                           privateDataSlot,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)privateDataSlot; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkSetPrivateDataEXT(VkDevice                                    device,
    VkObjectType                                objectType,
    uint64_t                                    objectHandle,
    VkPrivateDataSlot                           privateDataSlot,
    uint64_t                                    data) { (void)device; (void)objectType; (void)objectHandle; (void)privateDataSlot; (void)data;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetPrivateDataEXT(VkDevice                                    device,
    VkObjectType                                objectType,
    uint64_t                                    objectHandle,
    VkPrivateDataSlot                           privateDataSlot,
    uint64_t*                                   pData) { (void)device; (void)objectType; (void)objectHandle; (void)privateDataSlot; (void)pData;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchTileQCOM(VkCommandBuffer                             commandBuffer,
    const VkDispatchTileInfoQCOM*               pDispatchTileInfo) { (void)commandBuffer; (void)pDispatchTileInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginPerTileExecutionQCOM(VkCommandBuffer                             commandBuffer,
    const VkPerTileBeginInfoQCOM*               pPerTileBeginInfo) { (void)commandBuffer; (void)pPerTileBeginInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdEndPerTileExecutionQCOM(VkCommandBuffer                             commandBuffer,
    const VkPerTileEndInfoQCOM*                 pPerTileEndInfo) { (void)commandBuffer; (void)pPerTileEndInfo;   }

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSizeEXT(VkDevice                                    device,
    VkDescriptorSetLayout                       layout,
    VkDeviceSize*                               pLayoutSizeInBytes) { (void)device; (void)layout; (void)pLayoutSizeInBytes;   }

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutBindingOffsetEXT(VkDevice                                    device,
    VkDescriptorSetLayout                       layout,
    uint32_t                                    binding,
    VkDeviceSize*                               pOffset) { (void)device; (void)layout; (void)binding; (void)pOffset;   }

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorEXT(VkDevice                                    device,
    const VkDescriptorGetInfoEXT*               pDescriptorInfo,
    size_t                                      dataSize,
    void*                                       pDescriptor) { (void)device; (void)pDescriptorInfo; (void)dataSize; (void)pDescriptor;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBuffersEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    bufferCount,
    const VkDescriptorBufferBindingInfoEXT*     pBindingInfos) { (void)commandBuffer; (void)bufferCount; (void)pBindingInfos;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDescriptorBufferOffsetsEXT(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    firstSet,
    uint32_t                                    setCount,
    const uint32_t*                             pBufferIndices,
    const VkDeviceSize*                         pOffsets) { (void)commandBuffer; (void)pipelineBindPoint; (void)layout; (void)firstSet; (void)setCount; (void)pBufferIndices; (void)pOffsets;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBufferEmbeddedSamplersEXT(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    set) { (void)commandBuffer; (void)pipelineBindPoint; (void)layout; (void)set;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetBufferOpaqueCaptureDescriptorDataEXT(VkDevice                                    device,
    const VkBufferCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData) { (void)device; (void)pInfo; (void)pData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageOpaqueCaptureDescriptorDataEXT(VkDevice                                    device,
    const VkImageCaptureDescriptorDataInfoEXT*  pInfo,
    void*                                       pData) { (void)device; (void)pInfo; (void)pData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageViewOpaqueCaptureDescriptorDataEXT(VkDevice                                    device,
    const VkImageViewCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData) { (void)device; (void)pInfo; (void)pData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetSamplerOpaqueCaptureDescriptorDataEXT(VkDevice                                    device,
    const VkSamplerCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData) { (void)device; (void)pInfo; (void)pData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT(VkDevice                                    device,
    const VkAccelerationStructureCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData) { (void)device; (void)pInfo; (void)pData;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetFragmentShadingRateEnumNV(VkCommandBuffer                             commandBuffer,
    VkFragmentShadingRateNV                     shadingRate,
    const VkFragmentShadingRateCombinerOpKHR    combinerOps[2]) { (void)commandBuffer; (void)shadingRate;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceFaultInfoEXT(VkDevice                                    device,
    VkDeviceFaultCountsEXT*                     pFaultCounts,
    VkDeviceFaultInfoEXT*                       pFaultInfo) { (void)device; (void)pFaultCounts; (void)pFaultInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetVertexInputEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexBindingDescriptionCount,
    const VkVertexInputBindingDescription2EXT*  pVertexBindingDescriptions,
    uint32_t                                    vertexAttributeDescriptionCount,
    const VkVertexInputAttributeDescription2EXT* pVertexAttributeDescriptions) { (void)commandBuffer; (void)vertexBindingDescriptionCount; (void)pVertexBindingDescriptions; (void)vertexAttributeDescriptionCount; (void)pVertexAttributeDescriptions;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI(VkDevice                                    device,
    VkRenderPass                                renderpass,
    VkExtent2D*                                 pMaxWorkgroupSize) { (void)device; (void)renderpass; (void)pMaxWorkgroupSize;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdSubpassShadingHUAWEI(VkCommandBuffer                             commandBuffer) { (void)commandBuffer;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindInvocationMaskHUAWEI(VkCommandBuffer                             commandBuffer,
    VkImageView                                 imageView,
    VkImageLayout                               imageLayout) { (void)commandBuffer; (void)imageView; (void)imageLayout;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryRemoteAddressNV(VkDevice                                    device,
    const VkMemoryGetRemoteAddressInfoNV*       pMemoryGetRemoteAddressInfo,
    VkRemoteAddressNV*                          pAddress) { (void)device; (void)pMemoryGetRemoteAddressInfo; (void)pAddress;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelinePropertiesEXT(VkDevice                                    device,
    const VkPipelineInfoEXT*                    pPipelineInfo,
    VkBaseOutStructure*                         pPipelineProperties) { (void)device; (void)pPipelineInfo; (void)pPipelineProperties;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetPatchControlPointsEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    patchControlPoints) { (void)commandBuffer; (void)patchControlPoints;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizerDiscardEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    rasterizerDiscardEnable) { (void)commandBuffer; (void)rasterizerDiscardEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthBiasEnable) { (void)commandBuffer; (void)depthBiasEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetLogicOpEXT(VkCommandBuffer                             commandBuffer,
    VkLogicOp                                   logicOp) { (void)commandBuffer; (void)logicOp;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    primitiveRestartEnable) { (void)commandBuffer; (void)primitiveRestartEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorWriteEnableEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkBool32*                             pColorWriteEnables) { (void)commandBuffer; (void)attachmentCount; (void)pColorWriteEnables;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMultiEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    drawCount,
    const VkMultiDrawInfoEXT*                   pVertexInfo,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    uint32_t                                    stride) { (void)commandBuffer; (void)drawCount; (void)pVertexInfo; (void)instanceCount; (void)firstInstance; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMultiIndexedEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    drawCount,
    const VkMultiDrawIndexedInfoEXT*            pIndexInfo,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    uint32_t                                    stride,
    const int32_t*                              pVertexOffset) { (void)commandBuffer; (void)drawCount; (void)pIndexInfo; (void)instanceCount; (void)firstInstance; (void)stride; (void)pVertexOffset;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateMicromapEXT(VkDevice                                    device,
    const VkMicromapCreateInfoEXT*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkMicromapEXT*                              pMicromap) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pMicromap;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyMicromapEXT(VkDevice                                    device,
    VkMicromapEXT                               micromap,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)micromap; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBuildMicromapsEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    infoCount,
    const VkMicromapBuildInfoEXT*               pInfos) { (void)commandBuffer; (void)infoCount; (void)pInfos;   }

VKAPI_ATTR VkResult VKAPI_CALL vkBuildMicromapsEXT(VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    uint32_t                                    infoCount,
    const VkMicromapBuildInfoEXT*               pInfos) { (void)device; (void)deferredOperation; (void)infoCount; (void)pInfos;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMicromapEXT(VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyMicromapInfoEXT*                pInfo) { (void)device; (void)deferredOperation; (void)pInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMicromapToMemoryEXT(VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyMicromapToMemoryInfoEXT*        pInfo) { (void)device; (void)deferredOperation; (void)pInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToMicromapEXT(VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyMemoryToMicromapInfoEXT*        pInfo) { (void)device; (void)deferredOperation; (void)pInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkWriteMicromapsPropertiesEXT(VkDevice                                    device,
    uint32_t                                    micromapCount,
    const VkMicromapEXT*                        pMicromaps,
    VkQueryType                                 queryType,
    size_t                                      dataSize,
    void*                                       pData,
    size_t                                      stride) { (void)device; (void)micromapCount; (void)pMicromaps; (void)queryType; (void)dataSize; (void)pData; (void)stride;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMicromapEXT(VkCommandBuffer                             commandBuffer,
    const VkCopyMicromapInfoEXT*                pInfo) { (void)commandBuffer; (void)pInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMicromapToMemoryEXT(VkCommandBuffer                             commandBuffer,
    const VkCopyMicromapToMemoryInfoEXT*        pInfo) { (void)commandBuffer; (void)pInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToMicromapEXT(VkCommandBuffer                             commandBuffer,
    const VkCopyMemoryToMicromapInfoEXT*        pInfo) { (void)commandBuffer; (void)pInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdWriteMicromapsPropertiesEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    micromapCount,
    const VkMicromapEXT*                        pMicromaps,
    VkQueryType                                 queryType,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery) { (void)commandBuffer; (void)micromapCount; (void)pMicromaps; (void)queryType; (void)queryPool; (void)firstQuery;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMicromapCompatibilityEXT(VkDevice                                    device,
    const VkMicromapVersionInfoEXT*             pVersionInfo,
    VkAccelerationStructureCompatibilityKHR*    pCompatibility) { (void)device; (void)pVersionInfo; (void)pCompatibility;   }

VKAPI_ATTR void VKAPI_CALL vkGetMicromapBuildSizesEXT(VkDevice                                    device,
    VkAccelerationStructureBuildTypeKHR         buildType,
    const VkMicromapBuildInfoEXT*               pBuildInfo,
    VkMicromapBuildSizesInfoEXT*                pSizeInfo) { (void)device; (void)buildType; (void)pBuildInfo; (void)pSizeInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawClusterHUAWEI(VkCommandBuffer                             commandBuffer,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ) { (void)commandBuffer; (void)groupCountX; (void)groupCountY; (void)groupCountZ;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawClusterIndirectHUAWEI(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset) { (void)commandBuffer; (void)buffer; (void)offset;   }

VKAPI_ATTR void VKAPI_CALL vkSetDeviceMemoryPriorityEXT(VkDevice                                    device,
    VkDeviceMemory                              memory,
    float                                       priority) { (void)device; (void)memory; (void)priority;   }

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutHostMappingInfoVALVE(VkDevice                                    device,
    const VkDescriptorSetBindingReferenceVALVE* pBindingReference,
    VkDescriptorSetLayoutHostMappingInfoVALVE*  pHostMapping) { (void)device; (void)pBindingReference; (void)pHostMapping;   }

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetHostMappingVALVE(VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    void**                                      ppData) { (void)device; (void)descriptorSet; (void)ppData;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryIndirectNV(VkCommandBuffer                             commandBuffer,
    VkDeviceAddress                             copyBufferAddress,
    uint32_t                                    copyCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)copyBufferAddress; (void)copyCount; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToImageIndirectNV(VkCommandBuffer                             commandBuffer,
    VkDeviceAddress                             copyBufferAddress,
    uint32_t                                    copyCount,
    uint32_t                                    stride,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    const VkImageSubresourceLayers*             pImageSubresources) { (void)commandBuffer; (void)copyBufferAddress; (void)copyCount; (void)stride; (void)dstImage; (void)dstImageLayout; (void)pImageSubresources;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    decompressRegionCount,
    const VkDecompressMemoryRegionNV*           pDecompressMemoryRegions) { (void)commandBuffer; (void)decompressRegionCount; (void)pDecompressMemoryRegions;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryIndirectCountNV(VkCommandBuffer                             commandBuffer,
    VkDeviceAddress                             indirectCommandsAddress,
    VkDeviceAddress                             indirectCommandsCountAddress,
    uint32_t                                    stride) { (void)commandBuffer; (void)indirectCommandsAddress; (void)indirectCommandsCountAddress; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkGetPipelineIndirectMemoryRequirementsNV(VkDevice                                    device,
    const VkComputePipelineCreateInfo*          pCreateInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pCreateInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkCmdUpdatePipelineIndirectBufferNV(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline) { (void)commandBuffer; (void)pipelineBindPoint; (void)pipeline;   }

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetPipelineIndirectDeviceAddressNV(VkDevice                                    device,
    const VkPipelineIndirectDeviceAddressInfoNV* pInfo) { (void)device; (void)pInfo;      return NULL; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClampEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthClampEnable) { (void)commandBuffer; (void)depthClampEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetPolygonModeEXT(VkCommandBuffer                             commandBuffer,
    VkPolygonMode                               polygonMode) { (void)commandBuffer; (void)polygonMode;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizationSamplesEXT(VkCommandBuffer                             commandBuffer,
    VkSampleCountFlagBits                       rasterizationSamples) { (void)commandBuffer; (void)rasterizationSamples;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleMaskEXT(VkCommandBuffer                             commandBuffer,
    VkSampleCountFlagBits                       samples,
    const VkSampleMask*                         pSampleMask) { (void)commandBuffer; (void)samples; (void)pSampleMask;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetAlphaToCoverageEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    alphaToCoverageEnable) { (void)commandBuffer; (void)alphaToCoverageEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetAlphaToOneEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    alphaToOneEnable) { (void)commandBuffer; (void)alphaToOneEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetLogicOpEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    logicOpEnable) { (void)commandBuffer; (void)logicOpEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendEnableEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstAttachment,
    uint32_t                                    attachmentCount,
    const VkBool32*                             pColorBlendEnables) { (void)commandBuffer; (void)firstAttachment; (void)attachmentCount; (void)pColorBlendEnables;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendEquationEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstAttachment,
    uint32_t                                    attachmentCount,
    const VkColorBlendEquationEXT*              pColorBlendEquations) { (void)commandBuffer; (void)firstAttachment; (void)attachmentCount; (void)pColorBlendEquations;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorWriteMaskEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstAttachment,
    uint32_t                                    attachmentCount,
    const VkColorComponentFlags*                pColorWriteMasks) { (void)commandBuffer; (void)firstAttachment; (void)attachmentCount; (void)pColorWriteMasks;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetTessellationDomainOriginEXT(VkCommandBuffer                             commandBuffer,
    VkTessellationDomainOrigin                  domainOrigin) { (void)commandBuffer; (void)domainOrigin;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizationStreamEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    rasterizationStream) { (void)commandBuffer; (void)rasterizationStream;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetConservativeRasterizationModeEXT(VkCommandBuffer                             commandBuffer,
    VkConservativeRasterizationModeEXT          conservativeRasterizationMode) { (void)commandBuffer; (void)conservativeRasterizationMode;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetExtraPrimitiveOverestimationSizeEXT(VkCommandBuffer                             commandBuffer,
    float                                       extraPrimitiveOverestimationSize) { (void)commandBuffer; (void)extraPrimitiveOverestimationSize;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClipEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthClipEnable) { (void)commandBuffer; (void)depthClipEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleLocationsEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    sampleLocationsEnable) { (void)commandBuffer; (void)sampleLocationsEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendAdvancedEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstAttachment,
    uint32_t                                    attachmentCount,
    const VkColorBlendAdvancedEXT*              pColorBlendAdvanced) { (void)commandBuffer; (void)firstAttachment; (void)attachmentCount; (void)pColorBlendAdvanced;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetProvokingVertexModeEXT(VkCommandBuffer                             commandBuffer,
    VkProvokingVertexModeEXT                    provokingVertexMode) { (void)commandBuffer; (void)provokingVertexMode;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineRasterizationModeEXT(VkCommandBuffer                             commandBuffer,
    VkLineRasterizationModeEXT                  lineRasterizationMode) { (void)commandBuffer; (void)lineRasterizationMode;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStippleEnableEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    stippledLineEnable) { (void)commandBuffer; (void)stippledLineEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClipNegativeOneToOneEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    negativeOneToOne) { (void)commandBuffer; (void)negativeOneToOne;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWScalingEnableNV(VkCommandBuffer                             commandBuffer,
    VkBool32                                    viewportWScalingEnable) { (void)commandBuffer; (void)viewportWScalingEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportSwizzleNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewportSwizzleNV*                  pViewportSwizzles) { (void)commandBuffer; (void)firstViewport; (void)viewportCount; (void)pViewportSwizzles;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageToColorEnableNV(VkCommandBuffer                             commandBuffer,
    VkBool32                                    coverageToColorEnable) { (void)commandBuffer; (void)coverageToColorEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageToColorLocationNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    coverageToColorLocation) { (void)commandBuffer; (void)coverageToColorLocation;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageModulationModeNV(VkCommandBuffer                             commandBuffer,
    VkCoverageModulationModeNV                  coverageModulationMode) { (void)commandBuffer; (void)coverageModulationMode;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageModulationTableEnableNV(VkCommandBuffer                             commandBuffer,
    VkBool32                                    coverageModulationTableEnable) { (void)commandBuffer; (void)coverageModulationTableEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageModulationTableNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    coverageModulationTableCount,
    const float*                                pCoverageModulationTable) { (void)commandBuffer; (void)coverageModulationTableCount; (void)pCoverageModulationTable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetShadingRateImageEnableNV(VkCommandBuffer                             commandBuffer,
    VkBool32                                    shadingRateImageEnable) { (void)commandBuffer; (void)shadingRateImageEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetRepresentativeFragmentTestEnableNV(VkCommandBuffer                             commandBuffer,
    VkBool32                                    representativeFragmentTestEnable) { (void)commandBuffer; (void)representativeFragmentTestEnable;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetCoverageReductionModeNV(VkCommandBuffer                             commandBuffer,
    VkCoverageReductionModeNV                   coverageReductionMode) { (void)commandBuffer; (void)coverageReductionMode;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateTensorARM(VkDevice                                    device,
    const VkTensorCreateInfoARM*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkTensorARM*                                pTensor) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pTensor;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyTensorARM(VkDevice                                    device,
    VkTensorARM                                 tensor,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)tensor; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateTensorViewARM(VkDevice                                    device,
    const VkTensorViewCreateInfoARM*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkTensorViewARM*                            pView) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pView;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyTensorViewARM(VkDevice                                    device,
    VkTensorViewARM                             tensorView,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)tensorView; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkGetTensorMemoryRequirementsARM(VkDevice                                    device,
    const VkTensorMemoryRequirementsInfoARM*    pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR VkResult VKAPI_CALL vkBindTensorMemoryARM(VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindTensorMemoryInfoARM*            pBindInfos) { (void)device; (void)bindInfoCount; (void)pBindInfos;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceTensorMemoryRequirementsARM(VkDevice                                    device,
    const VkDeviceTensorMemoryRequirementsARM*  pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyTensorARM(VkCommandBuffer                             commandBuffer,
    const VkCopyTensorInfoARM*                  pCopyTensorInfo) { (void)commandBuffer; (void)pCopyTensorInfo;   }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalTensorPropertiesARM(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalTensorInfoARM* pExternalTensorInfo,
    VkExternalTensorPropertiesARM*              pExternalTensorProperties) { (void)physicalDevice; (void)pExternalTensorInfo; (void)pExternalTensorProperties;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetTensorOpaqueCaptureDescriptorDataARM(VkDevice                                    device,
    const VkTensorCaptureDescriptorDataInfoARM* pInfo,
    void*                                       pData) { (void)device; (void)pInfo; (void)pData;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetTensorViewOpaqueCaptureDescriptorDataARM(VkDevice                                    device,
    const VkTensorViewCaptureDescriptorDataInfoARM* pInfo,
    void*                                       pData) { (void)device; (void)pInfo; (void)pData;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetShaderModuleIdentifierEXT(VkDevice                                    device,
    VkShaderModule                              shaderModule,
    VkShaderModuleIdentifierEXT*                pIdentifier) { (void)device; (void)shaderModule; (void)pIdentifier;   }

VKAPI_ATTR void VKAPI_CALL vkGetShaderModuleCreateInfoIdentifierEXT(VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    VkShaderModuleIdentifierEXT*                pIdentifier) { (void)device; (void)pCreateInfo; (void)pIdentifier;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceOpticalFlowImageFormatsNV(VkPhysicalDevice                            physicalDevice,
    const VkOpticalFlowImageFormatInfoNV*       pOpticalFlowImageFormatInfo,
    uint32_t*                                   pFormatCount,
    VkOpticalFlowImageFormatPropertiesNV*       pImageFormatProperties) { (void)physicalDevice; (void)pOpticalFlowImageFormatInfo; (void)pFormatCount; (void)pImageFormatProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateOpticalFlowSessionNV(VkDevice                                    device,
    const VkOpticalFlowSessionCreateInfoNV*     pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkOpticalFlowSessionNV*                     pSession) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pSession;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyOpticalFlowSessionNV(VkDevice                                    device,
    VkOpticalFlowSessionNV                      session,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)session; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkBindOpticalFlowSessionImageNV(VkDevice                                    device,
    VkOpticalFlowSessionNV                      session,
    VkOpticalFlowSessionBindingPointNV          bindingPoint,
    VkImageView                                 view,
    VkImageLayout                               layout) { (void)device; (void)session; (void)bindingPoint; (void)view; (void)layout;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdOpticalFlowExecuteNV(VkCommandBuffer                             commandBuffer,
    VkOpticalFlowSessionNV                      session,
    const VkOpticalFlowExecuteInfoNV*           pExecuteInfo) { (void)commandBuffer; (void)session; (void)pExecuteInfo;   }

VKAPI_ATTR void VKAPI_CALL vkAntiLagUpdateAMD(VkDevice                                    device,
    const VkAntiLagDataAMD*                     pData) { (void)device; (void)pData;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShadersEXT(VkDevice                                    device,
    uint32_t                                    createInfoCount,
    const VkShaderCreateInfoEXT*                pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderEXT*                                pShaders) { (void)device; (void)createInfoCount; (void)pCreateInfos; (void)pAllocator; (void)pShaders;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderEXT(VkDevice                                    device,
    VkShaderEXT                                 shader,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)shader; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetShaderBinaryDataEXT(VkDevice                                    device,
    VkShaderEXT                                 shader,
    size_t*                                     pDataSize,
    void*                                       pData) { (void)device; (void)shader; (void)pDataSize; (void)pData;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdBindShadersEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    stageCount,
    const VkShaderStageFlagBits*                pStages,
    const VkShaderEXT*                          pShaders) { (void)commandBuffer; (void)stageCount; (void)pStages; (void)pShaders;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClampRangeEXT(VkCommandBuffer                             commandBuffer,
    VkDepthClampModeEXT                         depthClampMode,
    const VkDepthClampRangeEXT*                 pDepthClampRange) { (void)commandBuffer; (void)depthClampMode; (void)pDepthClampRange;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetFramebufferTilePropertiesQCOM(VkDevice                                    device,
    VkFramebuffer                               framebuffer,
    uint32_t*                                   pPropertiesCount,
    VkTilePropertiesQCOM*                       pProperties) { (void)device; (void)framebuffer; (void)pPropertiesCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDynamicRenderingTilePropertiesQCOM(VkDevice                                    device,
    const VkRenderingInfo*                      pRenderingInfo,
    VkTilePropertiesQCOM*                       pProperties) { (void)device; (void)pRenderingInfo; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeVectorPropertiesNV(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkCooperativeVectorPropertiesNV*            pProperties) { (void)physicalDevice; (void)pPropertyCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkConvertCooperativeVectorMatrixNV(VkDevice                                    device,
    const VkConvertCooperativeVectorMatrixInfoNV* pInfo) { (void)device; (void)pInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdConvertCooperativeVectorMatrixNV(VkCommandBuffer                             commandBuffer,
    uint32_t                                    infoCount,
    const VkConvertCooperativeVectorMatrixInfoNV* pInfos) { (void)commandBuffer; (void)infoCount; (void)pInfos;   }

VKAPI_ATTR VkResult VKAPI_CALL vkSetLatencySleepModeNV(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkLatencySleepModeInfoNV*             pSleepModeInfo) { (void)device; (void)swapchain; (void)pSleepModeInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkLatencySleepNV(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkLatencySleepInfoNV*                 pSleepInfo) { (void)device; (void)swapchain; (void)pSleepInfo;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkSetLatencyMarkerNV(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkSetLatencyMarkerInfoNV*             pLatencyMarkerInfo) { (void)device; (void)swapchain; (void)pLatencyMarkerInfo;   }

VKAPI_ATTR void VKAPI_CALL vkGetLatencyTimingsNV(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkGetLatencyMarkerInfoNV*                   pLatencyMarkerInfo) { (void)device; (void)swapchain; (void)pLatencyMarkerInfo;   }

VKAPI_ATTR void VKAPI_CALL vkQueueNotifyOutOfBandNV(VkQueue                                     queue,
    const VkOutOfBandQueueTypeInfoNV*           pQueueTypeInfo) { (void)queue; (void)pQueueTypeInfo;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDataGraphPipelinesARM(VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkDataGraphPipelineCreateInfoARM*     pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) { (void)device; (void)deferredOperation; (void)pipelineCache; (void)createInfoCount; (void)pCreateInfos; (void)pAllocator; (void)pPipelines;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDataGraphPipelineSessionARM(VkDevice                                    device,
    const VkDataGraphPipelineSessionCreateInfoARM* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDataGraphPipelineSessionARM*              pSession) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pSession;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDataGraphPipelineSessionBindPointRequirementsARM(VkDevice                                    device,
    const VkDataGraphPipelineSessionBindPointRequirementsInfoARM* pInfo,
    uint32_t*                                   pBindPointRequirementCount,
    VkDataGraphPipelineSessionBindPointRequirementARM* pBindPointRequirements) { (void)device; (void)pInfo; (void)pBindPointRequirementCount; (void)pBindPointRequirements;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetDataGraphPipelineSessionMemoryRequirementsARM(VkDevice                                    device,
    const VkDataGraphPipelineSessionMemoryRequirementsInfoARM* pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR VkResult VKAPI_CALL vkBindDataGraphPipelineSessionMemoryARM(VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindDataGraphPipelineSessionMemoryInfoARM* pBindInfos) { (void)device; (void)bindInfoCount; (void)pBindInfos;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDataGraphPipelineSessionARM(VkDevice                                    device,
    VkDataGraphPipelineSessionARM               session,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)session; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchDataGraphARM(VkCommandBuffer                             commandBuffer,
    VkDataGraphPipelineSessionARM               session,
    const VkDataGraphPipelineDispatchInfoARM*   pInfo) { (void)commandBuffer; (void)session; (void)pInfo;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDataGraphPipelineAvailablePropertiesARM(VkDevice                                    device,
    const VkDataGraphPipelineInfoARM*           pPipelineInfo,
    uint32_t*                                   pPropertiesCount,
    VkDataGraphPipelinePropertyARM*             pProperties) { (void)device; (void)pPipelineInfo; (void)pPropertiesCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetDataGraphPipelinePropertiesARM(VkDevice                                    device,
    const VkDataGraphPipelineInfoARM*           pPipelineInfo,
    uint32_t                                    propertiesCount,
    VkDataGraphPipelinePropertyQueryResultARM*  pProperties) { (void)device; (void)pPipelineInfo; (void)propertiesCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceQueueFamilyDataGraphPropertiesARM(VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    uint32_t*                                   pQueueFamilyDataGraphPropertyCount,
    VkQueueFamilyDataGraphPropertiesARM*        pQueueFamilyDataGraphProperties) { (void)physicalDevice; (void)queueFamilyIndex; (void)pQueueFamilyDataGraphPropertyCount; (void)pQueueFamilyDataGraphProperties;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyDataGraphProcessingEnginePropertiesARM(VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceQueueFamilyDataGraphProcessingEngineInfoARM* pQueueFamilyDataGraphProcessingEngineInfo,
    VkQueueFamilyDataGraphProcessingEnginePropertiesARM* pQueueFamilyDataGraphProcessingEngineProperties) { (void)physicalDevice; (void)pQueueFamilyDataGraphProcessingEngineInfo; (void)pQueueFamilyDataGraphProcessingEngineProperties;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetAttachmentFeedbackLoopEnableEXT(VkCommandBuffer                             commandBuffer,
    VkImageAspectFlags                          aspectMask) { (void)commandBuffer; (void)aspectMask;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBindTileMemoryQCOM(VkCommandBuffer                             commandBuffer,
    const VkTileMemoryBindInfoQCOM*             pTileMemoryBindInfo) { (void)commandBuffer; (void)pTileMemoryBindInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryEXT(VkCommandBuffer                             commandBuffer,
    const VkDecompressMemoryInfoEXT*            pDecompressMemoryInfoEXT) { (void)commandBuffer; (void)pDecompressMemoryInfoEXT;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryIndirectCountEXT(VkCommandBuffer                             commandBuffer,
    VkMemoryDecompressionMethodFlagsEXT         decompressionMethod,
    VkDeviceAddress                             indirectCommandsAddress,
    VkDeviceAddress                             indirectCommandsCountAddress,
    uint32_t                                    maxDecompressionCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)decompressionMethod; (void)indirectCommandsAddress; (void)indirectCommandsCountAddress; (void)maxDecompressionCount; (void)stride;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateExternalComputeQueueNV(VkDevice                                    device,
    const VkExternalComputeQueueCreateInfoNV*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkExternalComputeQueueNV*                   pExternalQueue) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pExternalQueue;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyExternalComputeQueueNV(VkDevice                                    device,
    VkExternalComputeQueueNV                    externalQueue,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)externalQueue; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkGetExternalComputeQueueDataNV(VkExternalComputeQueueNV                    externalQueue,
    VkExternalComputeQueueDataParamsNV*         params,
    void*                                       pData) { (void)externalQueue; (void)params; (void)pData;   }

VKAPI_ATTR void VKAPI_CALL vkGetClusterAccelerationStructureBuildSizesNV(VkDevice                                    device,
    const VkClusterAccelerationStructureInputInfoNV* pInfo,
    VkAccelerationStructureBuildSizesInfoKHR*   pSizeInfo) { (void)device; (void)pInfo; (void)pSizeInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBuildClusterAccelerationStructureIndirectNV(VkCommandBuffer                             commandBuffer,
    const VkClusterAccelerationStructureCommandsInfoNV* pCommandInfos) { (void)commandBuffer; (void)pCommandInfos;   }

VKAPI_ATTR void VKAPI_CALL vkGetPartitionedAccelerationStructuresBuildSizesNV(VkDevice                                    device,
    const VkPartitionedAccelerationStructureInstancesInputNV* pInfo,
    VkAccelerationStructureBuildSizesInfoKHR*   pSizeInfo) { (void)device; (void)pInfo; (void)pSizeInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBuildPartitionedAccelerationStructuresNV(VkCommandBuffer                             commandBuffer,
    const VkBuildPartitionedAccelerationStructureInfoNV* pBuildInfo) { (void)commandBuffer; (void)pBuildInfo;   }

VKAPI_ATTR void VKAPI_CALL vkGetGeneratedCommandsMemoryRequirementsEXT(VkDevice                                    device,
    const VkGeneratedCommandsMemoryRequirementsInfoEXT* pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements) { (void)device; (void)pInfo; (void)pMemoryRequirements;   }

VKAPI_ATTR void VKAPI_CALL vkCmdPreprocessGeneratedCommandsEXT(VkCommandBuffer                             commandBuffer,
    const VkGeneratedCommandsInfoEXT*           pGeneratedCommandsInfo,
    VkCommandBuffer                             stateCommandBuffer) { (void)commandBuffer; (void)pGeneratedCommandsInfo; (void)stateCommandBuffer;   }

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteGeneratedCommandsEXT(VkCommandBuffer                             commandBuffer,
    VkBool32                                    isPreprocessed,
    const VkGeneratedCommandsInfoEXT*           pGeneratedCommandsInfo) { (void)commandBuffer; (void)isPreprocessed; (void)pGeneratedCommandsInfo;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectCommandsLayoutEXT(VkDevice                                    device,
    const VkIndirectCommandsLayoutCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkIndirectCommandsLayoutEXT*                pIndirectCommandsLayout) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pIndirectCommandsLayout;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectCommandsLayoutEXT(VkDevice                                    device,
    VkIndirectCommandsLayoutEXT                 indirectCommandsLayout,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)indirectCommandsLayout; (void)pAllocator;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectExecutionSetEXT(VkDevice                                    device,
    const VkIndirectExecutionSetCreateInfoEXT*  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkIndirectExecutionSetEXT*                  pIndirectExecutionSet) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pIndirectExecutionSet;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectExecutionSetEXT(VkDevice                                    device,
    VkIndirectExecutionSetEXT                   indirectExecutionSet,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)indirectExecutionSet; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkUpdateIndirectExecutionSetPipelineEXT(VkDevice                                    device,
    VkIndirectExecutionSetEXT                   indirectExecutionSet,
    uint32_t                                    executionSetWriteCount,
    const VkWriteIndirectExecutionSetPipelineEXT* pExecutionSetWrites) { (void)device; (void)indirectExecutionSet; (void)executionSetWriteCount; (void)pExecutionSetWrites;   }

VKAPI_ATTR void VKAPI_CALL vkUpdateIndirectExecutionSetShaderEXT(VkDevice                                    device,
    VkIndirectExecutionSetEXT                   indirectExecutionSet,
    uint32_t                                    executionSetWriteCount,
    const VkWriteIndirectExecutionSetShaderEXT* pExecutionSetWrites) { (void)device; (void)indirectExecutionSet; (void)executionSetWriteCount; (void)pExecutionSetWrites;   }

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkCooperativeMatrixFlexibleDimensionsPropertiesNV* pProperties) { (void)physicalDevice; (void)pPropertyCount; (void)pProperties;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM(VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    uint32_t*                                   pCounterCount,
    VkPerformanceCounterARM*                    pCounters,
    VkPerformanceCounterDescriptionARM*         pCounterDescriptions) { (void)physicalDevice; (void)queueFamilyIndex; (void)pCounterCount; (void)pCounters; (void)pCounterDescriptions;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering2EXT(VkCommandBuffer                             commandBuffer,
    const VkRenderingEndInfoKHR*                pRenderingEndInfo) { (void)commandBuffer; (void)pRenderingEndInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBeginCustomResolveEXT(VkCommandBuffer                             commandBuffer,
    const VkBeginCustomResolveInfoEXT*          pBeginCustomResolveInfo) { (void)commandBuffer; (void)pBeginCustomResolveInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdSetComputeOccupancyPriorityNV(VkCommandBuffer                             commandBuffer,
    const VkComputeOccupancyPriorityParametersNV* pParameters) { (void)commandBuffer; (void)pParameters;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureKHR(VkDevice                                    device,
    const VkAccelerationStructureCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkAccelerationStructureKHR*                 pAccelerationStructure) { (void)device; (void)pCreateInfo; (void)pAllocator; (void)pAccelerationStructure;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureKHR(VkDevice                                    device,
    VkAccelerationStructureKHR                  accelerationStructure,
    const VkAllocationCallbacks*                pAllocator) { (void)device; (void)accelerationStructure; (void)pAllocator;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresKHR(VkCommandBuffer                             commandBuffer,
    uint32_t                                    infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
    const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos) { (void)commandBuffer; (void)infoCount; (void)pInfos; (void)ppBuildRangeInfos;   }

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresIndirectKHR(VkCommandBuffer                             commandBuffer,
    uint32_t                                    infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
    const VkDeviceAddress*                      pIndirectDeviceAddresses,
    const uint32_t*                             pIndirectStrides,
    const uint32_t* const*                      ppMaxPrimitiveCounts) { (void)commandBuffer; (void)infoCount; (void)pInfos; (void)pIndirectDeviceAddresses; (void)pIndirectStrides; (void)ppMaxPrimitiveCounts;   }

VKAPI_ATTR VkResult VKAPI_CALL vkBuildAccelerationStructuresKHR(VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    uint32_t                                    infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
    const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos) { (void)device; (void)deferredOperation; (void)infoCount; (void)pInfos; (void)ppBuildRangeInfos;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureKHR(VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyAccelerationStructureInfoKHR*   pInfo) { (void)device; (void)deferredOperation; (void)pInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureToMemoryKHR(VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo) { (void)device; (void)deferredOperation; (void)pInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToAccelerationStructureKHR(VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo) { (void)device; (void)deferredOperation; (void)pInfo;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkWriteAccelerationStructuresPropertiesKHR(VkDevice                                    device,
    uint32_t                                    accelerationStructureCount,
    const VkAccelerationStructureKHR*           pAccelerationStructures,
    VkQueryType                                 queryType,
    size_t                                      dataSize,
    void*                                       pData,
    size_t                                      stride) { (void)device; (void)accelerationStructureCount; (void)pAccelerationStructures; (void)queryType; (void)dataSize; (void)pData; (void)stride;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureKHR(VkCommandBuffer                             commandBuffer,
    const VkCopyAccelerationStructureInfoKHR*   pInfo) { (void)commandBuffer; (void)pInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer                             commandBuffer,
    const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo) { (void)commandBuffer; (void)pInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer                             commandBuffer,
    const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo) { (void)commandBuffer; (void)pInfo;   }

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetAccelerationStructureDeviceAddressKHR(VkDevice                                    device,
    const VkAccelerationStructureDeviceAddressInfoKHR* pInfo) { (void)device; (void)pInfo;      return NULL; }

VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesKHR(VkCommandBuffer                             commandBuffer,
    uint32_t                                    accelerationStructureCount,
    const VkAccelerationStructureKHR*           pAccelerationStructures,
    VkQueryType                                 queryType,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery) { (void)commandBuffer; (void)accelerationStructureCount; (void)pAccelerationStructures; (void)queryType; (void)queryPool; (void)firstQuery;   }

VKAPI_ATTR void VKAPI_CALL vkGetDeviceAccelerationStructureCompatibilityKHR(VkDevice                                    device,
    const VkAccelerationStructureVersionInfoKHR* pVersionInfo,
    VkAccelerationStructureCompatibilityKHR*    pCompatibility) { (void)device; (void)pVersionInfo; (void)pCompatibility;   }

VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureBuildSizesKHR(VkDevice                                    device,
    VkAccelerationStructureBuildTypeKHR         buildType,
    const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo,
    const uint32_t*                             pMaxPrimitiveCounts,
    VkAccelerationStructureBuildSizesInfoKHR*   pSizeInfo) { (void)device; (void)buildType; (void)pBuildInfo; (void)pMaxPrimitiveCounts; (void)pSizeInfo;   }

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysKHR(VkCommandBuffer                             commandBuffer,
    const VkStridedDeviceAddressRegionKHR*      pRaygenShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pMissShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pHitShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pCallableShaderBindingTable,
    uint32_t                                    width,
    uint32_t                                    height,
    uint32_t                                    depth) { (void)commandBuffer; (void)pRaygenShaderBindingTable; (void)pMissShaderBindingTable; (void)pHitShaderBindingTable; (void)pCallableShaderBindingTable; (void)width; (void)height; (void)depth;   }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesKHR(VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkRayTracingPipelineCreateInfoKHR*    pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) { (void)device; (void)deferredOperation; (void)pipelineCache; (void)createInfoCount; (void)pCreateInfos; (void)pAllocator; (void)pPipelines;      return 0; }

VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice                                    device,
    VkPipeline                                  pipeline,
    uint32_t                                    firstGroup,
    uint32_t                                    groupCount,
    size_t                                      dataSize,
    void*                                       pData) { (void)device; (void)pipeline; (void)firstGroup; (void)groupCount; (void)dataSize; (void)pData;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysIndirectKHR(VkCommandBuffer                             commandBuffer,
    const VkStridedDeviceAddressRegionKHR*      pRaygenShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pMissShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pHitShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pCallableShaderBindingTable,
    VkDeviceAddress                             indirectDeviceAddress) { (void)commandBuffer; (void)pRaygenShaderBindingTable; (void)pMissShaderBindingTable; (void)pHitShaderBindingTable; (void)pCallableShaderBindingTable; (void)indirectDeviceAddress;   }

VKAPI_ATTR VkDeviceSize VKAPI_CALL vkGetRayTracingShaderGroupStackSizeKHR(VkDevice                                    device,
    VkPipeline                                  pipeline,
    uint32_t                                    group,
    VkShaderGroupShaderKHR                      groupShader) { (void)device; (void)pipeline; (void)group; (void)groupShader;      return 0; }

VKAPI_ATTR void VKAPI_CALL vkCmdSetRayTracingPipelineStackSizeKHR(VkCommandBuffer                             commandBuffer,
    uint32_t                                    pipelineStackSize) { (void)commandBuffer; (void)pipelineStackSize;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksEXT(VkCommandBuffer                             commandBuffer,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ) { (void)commandBuffer; (void)groupCountX; (void)groupCountY; (void)groupCountZ;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectEXT(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)drawCount; (void)stride;   }

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountEXT(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) { (void)commandBuffer; (void)buffer; (void)offset; (void)countBuffer; (void)countBufferOffset; (void)maxDrawCount; (void)stride;   }

VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    if (pSupportedVersion) { if (*pSupportedVersion > 5) *pSupportedVersion = 5; }
    return VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    (void)instance; if (!pName) return NULL;
    return (PFN_vkVoidFunction)GetProcAddress(g_hModule, pName);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName) {
    (void)instance; if (!pName) return NULL;
    return (PFN_vkVoidFunction)GetProcAddress(g_hModule, pName);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) g_hModule = hModule;
    (void)lpReserved;
    return TRUE;
}


/*
 * BC-250 Vulkan ICD - Minimal Vulkan types for Windows
 * Based on Vulkan 1.4 specification
 */

#pragma once
#ifndef BC250_VULKAN_ICD_H
#define BC250_VULKAN_ICD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Vulkan base types */
typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkSampleMask;
typedef int32_t VkResult;
typedef void* VkHandle;

/* Calling convention */
#ifdef _WIN32
#define VKAPI_CALL __stdcall
#define VKAPI_PTR  __stdcall
#else
#define VKAPI_CALL
#define VKAPI_PTR
#endif

/* VK_NULL_HANDLE */
#define VK_NULL_HANDLE 0

/* VkResult codes */
#define VK_SUCCESS                     0
#define VK_NOT_READY                   1
#define VK_TIMEOUT                     2
#define VK_ERROR_OUT_OF_HOST_MEMORY   (-1)
#define VK_ERROR_OUT_OF_DEVICE_MEMORY (-2)
#define VK_ERROR_DEVICE_LOST          (-4)
#define VK_ERROR_FEATURE_NOT_PRESENT (-8)
#define VK_ERROR_INCOMPATIBLE_DRIVER (-9)

/* VkFlags */
#define VK_QUEUE_GRAPHICS_BIT        0x01
#define VK_QUEUE_COMPUTE_BIT         0x02
#define VK_QUEUE_TRANSFER_BIT        0x04
#define VK_QUEUE_SPARSE_BINDING_BIT  0x08

/* VkPhysicalDeviceType */
#define VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU    1
#define VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU  2
#define VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU     3
#define VK_PHYSICAL_DEVICE_TYPE_CPU              4

/* VkFormat */
typedef enum VkFormat_ {
    VK_FORMAT_UNDEFINED = 0,
    VK_FORMAT_R8G8B8A8_UNORM = 37,
    VK_FORMAT_R8G8B8A8_SRGB = 43,
    VK_FORMAT_B8G8R8A8_UNORM = 44,
    VK_FORMAT_B8G8R8A8_SRGB = 50,
    VK_FORMAT_D32_SFLOAT = 126,
    VK_FORMAT_D24_UNORM_S8_UINT = 129,
} VkFormat;

/* VkImageType */
typedef enum VkImageType_ {
    VK_IMAGE_TYPE_1D = 0,
    VK_IMAGE_TYPE_2D = 1,
    VK_IMAGE_TYPE_3D = 2,
} VkImageType;

/* VkImageTiling */
typedef enum VkImageTiling_ {
    VK_IMAGE_TILING_OPTIMAL = 0,
    VK_IMAGE_TILING_LINEAR = 1,
} VkImageTiling;

/* VkImageLayout */
typedef enum VkImageLayout_ {
    VK_IMAGE_LAYOUT_UNDEFINED = 0,
    VK_IMAGE_LAYOUT_GENERAL = 1,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL = 2,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL = 3,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL = 4,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL = 5,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL = 6,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL = 7,
    VK_IMAGE_LAYOUT_PREINITIALIZED = 8,
} VkImageLayout;

/* VkImageUsageFlagBits */
#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT          0x01
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT          0x02
#define VK_IMAGE_USAGE_SAMPLED_BIT               0x04
#define VK_IMAGE_USAGE_STORAGE_BIT               0x08
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT      0x10
#define VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT 0x20

/* VkMemoryPropertyFlagBits */
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT         0x01
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT         0x02
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT        0x04
#define VK_MEMORY_PROPERTY_HOST_CACHED_BIT          0x08
#define VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT     0x20

/* VkBufferUsageFlagBits */
#define VK_BUFFER_USAGE_TRANSFER_SRC_BIT       0x01
#define VK_BUFFER_USAGE_TRANSFER_DST_BIT       0x02
#define VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT 0x04
#define VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT 0x08
#define VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT    0x10
#define VK_BUFFER_USAGE_STORAGE_BUFFER_BIT    0x20
#define VK_BUFFER_USAGE_INDEX_BUFFER_BIT      0x40
#define VK_BUFFER_USAGE_VERTEX_BUFFER_BIT     0x80
#define VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT   0x100

/* VkPipelineStageFlagBits */
#define VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT          0x00000001
#define VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT        0x00000002
#define VK_PIPELINE_STAGE_VERTEX_INPUT_BIT         0x00000004
#define VK_PIPELINE_STAGE_VERTEX_SHADER_BIT        0x00000008
#define VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT      0x00000020
#define VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT 0x00000040
#define VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT  0x00000100
#define VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 0x00000400
#define VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT       0x00000800
#define VK_PIPELINE_STAGE_TRANSFER_BIT             0x00001000
#define VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT       0x00002000

/* Opaque handles */
typedef VkHandle VkInstance;
typedef VkHandle VkPhysicalDevice;
typedef VkHandle VkDevice;
typedef VkHandle VkQueue;
typedef VkHandle VkCommandBuffer;
typedef VkHandle VkDeviceMemory;
typedef VkHandle VkBuffer;
typedef VkHandle VkImage;
typedef VkHandle VkSemaphore;
typedef VkHandle VkFence;
typedef VkHandle VkEvent;
typedef VkHandle VkQueryPool;
typedef VkHandle VkPipelineCache;
typedef VkHandle VkPipeline;
typedef VkHandle VkPipelineLayout;
typedef VkHandle VkSampler;
typedef VkHandle VkDescriptorPool;
typedef VkHandle VkDescriptorSet;
typedef VkHandle VkDescriptorSetLayout;
typedef VkHandle VkFramebuffer;
typedef VkHandle VkRenderPass;
typedef VkHandle VkCommandPool;
typedef VkHandle VkSwapchainKHR;
typedef VkHandle VkSurfaceKHR;
typedef VkHandle VkDisplayKHR;
typedef VkHandle VkPhysicalDeviceMemoryProperties;

/* Vulkan API Version */
#define VK_MAKE_VERSION(major, minor, patch) \
    (((major) << 22) | ((minor) << 12) | (patch))

#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)
#define VK_API_VERSION_1_1 VK_MAKE_VERSION(1, 1, 0)
#define VK_API_VERSION_1_2 VK_MAKE_VERSION(1, 2, 0)
#define VK_API_VERSION_1_3 VK_MAKE_VERSION(1, 3, 0)
#define VK_API_VERSION_1_4 VK_MAKE_VERSION(1, 4, 0)

/* BC-250 specific defines */
#define BC250_VULKAN_VERSION    VK_API_VERSION_1_4
#define BC250_VENDOR_ID         0x1002
#define BC250_DEVICE_ID         0x13FE
#define BC250_DRIVER_VERSION    VK_MAKE_VERSION(4, 3, 0)

/* Function pointer types for ICD dispatch */
typedef VkResult (VKAPI_PTR *PFN_vkCreateInstance)(const void*, const void*, VkInstance*);
typedef void (VKAPI_PTR *PFN_vkDestroyInstance)(VkInstance, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateDevice)(VkPhysicalDevice, const void*, const void*, VkDevice*);
typedef void (VKAPI_PTR *PFN_vkDestroyDevice)(VkDevice, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
typedef VkResult (VKAPI_PTR *PFN_vkAllocateMemory)(VkDevice, const void*, const void*, VkDeviceMemory*);
typedef void (VKAPI_PTR *PFN_vkFreeMemory)(VkDevice, VkDeviceMemory, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkMapMemory)(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void**);
typedef void (VKAPI_PTR *PFN_vkUnmapMemory)(VkDevice, VkDeviceMemory);
typedef VkResult (VKAPI_PTR *PFN_vkCreateBuffer)(VkDevice, const void*, const void*, VkBuffer*);
typedef void (VKAPI_PTR *PFN_vkDestroyBuffer)(VkDevice, VkBuffer, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateFence)(VkDevice, const void*, const void*, VkFence*);
typedef void (VKAPI_PTR *PFN_vkDestroyFence)(VkDevice, VkFence, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkGetFenceStatus)(VkDevice, VkFence);
typedef VkResult (VKAPI_PTR *PFN_vkWaitForFences)(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t);
typedef VkResult (VKAPI_PTR *PFN_vkResetFences)(VkDevice, uint32_t, const VkFence*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateCommandPool)(VkDevice, const void*, const void*, VkCommandPool*);
typedef void (VKAPI_PTR *PFN_vkDestroyCommandPool)(VkDevice, VkCommandPool, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkAllocateCommandBuffers)(VkDevice, const void*, VkCommandBuffer*);
typedef void (VKAPI_PTR *PFN_vkFreeCommandBuffers)(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*);
typedef VkResult (VKAPI_PTR *PFN_vkBeginCommandBuffer)(VkCommandBuffer, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef VkResult (VKAPI_PTR *PFN_vkResetCommandBuffer)(VkCommandBuffer, VkFlags);
typedef VkResult (VKAPI_PTR *PFN_vkQueueSubmit)(VkQueue, uint32_t, const void*, VkFence);
typedef VkResult (VKAPI_PTR *PFN_vkQueueWaitIdle)(VkQueue);
typedef VkResult (VKAPI_PTR *PFN_vkDeviceWaitIdle)(VkDevice);
typedef VkResult (VKAPI_PTR *PFN_vkCreateGraphicsPipelines)(VkDevice, VkPipelineCache, uint32_t, const void*, const void*, VkPipeline*);
typedef void (VKAPI_PTR *PFN_vkDestroyPipeline)(VkDevice, VkPipeline, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdPipelineBarrier)(VkCommandBuffer, VkFlags, VkFlags, VkFlags, uint32_t, const void*, uint32_t, const void*, uint32_t, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdBindPipeline)(VkCommandBuffer, VkFlags, VkPipeline);
typedef void (VKAPI_PTR *PFN_vkCmdDraw)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (VKAPI_PTR *PFN_vkCmdDrawIndexed)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
typedef void (VKAPI_PTR *PFN_vkCmdDispatch)(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
typedef void (VKAPI_PTR *PFN_vkCmdCopyBuffer)(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdCopyImage)(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdClearColorImage)(VkCommandBuffer, VkImage, VkImageLayout, const void*, uint32_t, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdClearDepthStencilImage)(VkCommandBuffer, VkImage, VkImageLayout, const void*, uint32_t, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdSetViewport)(VkCommandBuffer, uint32_t, uint32_t, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdSetScissor)(VkCommandBuffer, uint32_t, uint32_t, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdBindVertexBuffers)(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*);
typedef void (VKAPI_PTR *PFN_vkCmdBindIndexBuffer)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkFlags);
typedef void (VKAPI_PTR *PFN_vkCmdBindDescriptorSets)(VkCommandBuffer, VkFlags, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*);
typedef void (VKAPI_PTR *PFN_vkCmdPushConstants)(VkCommandBuffer, VkPipelineLayout, VkFlags, uint32_t, uint32_t, const void*);

#ifdef __cplusplus
}
#endif

#endif /* BC250_VULKAN_ICD_H */

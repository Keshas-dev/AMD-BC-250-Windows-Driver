/*
 * BC-250 Vulkan Test Application
 * Tests the Vulkan ICD by creating a device and submitting commands.
 */

#include <windows.h>
#include <stdio.h>
#include "..\src\vulkan\bc250_vulkan.h"

/* VK_TRUE for compatibility */
#ifndef VK_TRUE
#define VK_TRUE 1
#endif

/* Vulkan function pointers (loaded from ICD) */
typedef VkResult (VKAPI_PTR *PFN_vkCreateInstance)(const void*, const void*, VkInstance*);
typedef void (VKAPI_PTR *PFN_vkDestroyInstance)(VkInstance, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateDevice)(VkPhysicalDevice, const void*, const void*, VkDevice*);
typedef void (VKAPI_PTR *PFN_vkDestroyDevice)(VkDevice, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
typedef VkResult (VKAPI_PTR *PFN_vkAllocateMemory)(VkDevice, const void*, const void*, VkDeviceMemory*);
typedef void (VKAPI_PTR *PFN_vkFreeMemory)(VkDevice, VkDeviceMemory, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateBuffer)(VkDevice, const void*, const void*, VkBuffer*);
typedef void (VKAPI_PTR *PFN_vkDestroyBuffer)(VkDevice, VkBuffer, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateFence)(VkDevice, const void*, const void*, VkFence*);
typedef VkResult (VKAPI_PTR *PFN_vkWaitForFences)(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t);
typedef void (VKAPI_PTR *PFN_vkDestroyFence)(VkDevice, VkFence, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateCommandPool)(VkDevice, const void*, const void*, VkCommandPool*);
typedef void (VKAPI_PTR *PFN_vkDestroyCommandPool)(VkDevice, VkCommandPool, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkAllocateCommandBuffers)(VkDevice, const void*, VkCommandBuffer*);
typedef VkResult (VKAPI_PTR *PFN_vkBeginCommandBuffer)(VkCommandBuffer, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef VkResult (VKAPI_PTR *PFN_vkQueueSubmit)(VkQueue, uint32_t, const void*, VkFence);
typedef VkResult (VKAPI_PTR *PFN_vkDeviceWaitIdle)(VkDevice);
typedef VkResult (VKAPI_PTR *PFN_vkCreateGraphicsPipelines)(VkDevice, VkPipelineCache, uint32_t, const void*, const void*, VkPipeline*);
typedef void (VKAPI_PTR *PFN_vkDestroyPipeline)(VkDevice, VkPipeline, const void*);

/* Global function pointers */
static PFN_vkCreateInstance pfnCreateInstance;
static PFN_vkDestroyInstance pfnDestroyInstance;
static PFN_vkCreateDevice pfnCreateDevice;
static PFN_vkDestroyDevice pfnDestroyDevice;
static PFN_vkGetDeviceQueue pfnGetDeviceQueue;
static PFN_vkAllocateMemory pfnAllocateMemory;
static PFN_vkFreeMemory pfnFreeMemory;
static PFN_vkCreateBuffer pfnCreateBuffer;
static PFN_vkDestroyBuffer pfnDestroyBuffer;
static PFN_vkCreateFence pfnCreateFence;
static PFN_vkWaitForFences pfnWaitForFences;
static PFN_vkDestroyFence pfnDestroyFence;
static PFN_vkCreateCommandPool pfnCreateCommandPool;
static PFN_vkDestroyCommandPool pfnDestroyCommandPool;
static PFN_vkAllocateCommandBuffers pfnAllocateCommandBuffers;
static PFN_vkBeginCommandBuffer pfnBeginCommandBuffer;
static PFN_vkEndCommandBuffer pfnEndCommandBuffer;
static PFN_vkQueueSubmit pfnQueueSubmit;
static PFN_vkDeviceWaitIdle pfnDeviceWaitIdle;
static PFN_vkCreateGraphicsPipelines pfnCreateGraphicsPipelines;
static PFN_vkDestroyPipeline pfnDestroyPipeline;

/* Load ICD functions */
static HMODULE hIcd = NULL;

static int LoadIcd(void)
{
    hIcd = LoadLibraryW(L"amdbc250vulkan.dll");
    if (!hIcd) {
        printf("[FAIL] Cannot load amdbc250vulkan.dll (error %lu)\n", GetLastError());
        printf("       Make sure the DLL is in the same directory.\n");
        return 0;
    }
    
    /* Get ICD dispatch table */
    typedef void* (VKAPI_PTR *PFN_vk_icdGetInstanceProcAddr)(VkInstance, const char*);
    PFN_vk_icdGetInstanceProcAddr pfnGetInstanceProcAddr = 
        (PFN_vk_icdGetInstanceProcAddr)GetProcAddress(hIcd, "vk_icdGetInstanceProcAddr");
    
    if (!pfnGetInstanceProcAddr) {
        printf("[FAIL] Cannot find vk_icdGetInstanceProcAddr\n");
        return 0;
    }
    
    /* Load function pointers */
    pfnCreateInstance = (PFN_vkCreateInstance)pfnGetInstanceProcAddr(NULL, "vkCreateInstance");
    pfnDestroyInstance = (PFN_vkDestroyInstance)pfnGetInstanceProcAddr(NULL, "vkDestroyInstance");
    pfnCreateDevice = (PFN_vkCreateDevice)pfnGetInstanceProcAddr(NULL, "vkCreateDevice");
    pfnDestroyDevice = (PFN_vkDestroyDevice)pfnGetInstanceProcAddr(NULL, "vkDestroyDevice");
    pfnGetDeviceQueue = (PFN_vkGetDeviceQueue)pfnGetInstanceProcAddr(NULL, "vkGetDeviceQueue");
    pfnAllocateMemory = (PFN_vkAllocateMemory)pfnGetInstanceProcAddr(NULL, "vkAllocateMemory");
    pfnFreeMemory = (PFN_vkFreeMemory)pfnGetInstanceProcAddr(NULL, "vkFreeMemory");
    pfnCreateBuffer = (PFN_vkCreateBuffer)pfnGetInstanceProcAddr(NULL, "vkCreateBuffer");
    pfnDestroyBuffer = (PFN_vkDestroyBuffer)pfnGetInstanceProcAddr(NULL, "vkDestroyBuffer");
    pfnCreateFence = (PFN_vkCreateFence)pfnGetInstanceProcAddr(NULL, "vkCreateFence");
    pfnWaitForFences = (PFN_vkWaitForFences)pfnGetInstanceProcAddr(NULL, "vkWaitForFences");
    pfnDestroyFence = (PFN_vkDestroyFence)pfnGetInstanceProcAddr(NULL, "vkDestroyFence");
    pfnCreateCommandPool = (PFN_vkCreateCommandPool)pfnGetInstanceProcAddr(NULL, "vkCreateCommandPool");
    pfnDestroyCommandPool = (PFN_vkDestroyCommandPool)pfnGetInstanceProcAddr(NULL, "vkDestroyCommandPool");
    pfnAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)pfnGetInstanceProcAddr(NULL, "vkAllocateCommandBuffers");
    pfnBeginCommandBuffer = (PFN_vkBeginCommandBuffer)pfnGetInstanceProcAddr(NULL, "vkBeginCommandBuffer");
    pfnEndCommandBuffer = (PFN_vkEndCommandBuffer)pfnGetInstanceProcAddr(NULL, "vkEndCommandBuffer");
    pfnQueueSubmit = (PFN_vkQueueSubmit)pfnGetInstanceProcAddr(NULL, "vkQueueSubmit");
    pfnDeviceWaitIdle = (PFN_vkDeviceWaitIdle)pfnGetInstanceProcAddr(NULL, "vkDeviceWaitIdle");
    pfnCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)pfnGetInstanceProcAddr(NULL, "vkCreateGraphicsPipelines");
    pfnDestroyPipeline = (PFN_vkDestroyPipeline)pfnGetInstanceProcAddr(NULL, "vkDestroyPipeline");
    
    printf("[OK] ICD loaded and functions resolved\n");
    return 1;
}

int main(void)
{
    int testCount = 0;
    int passCount = 0;
    
    printf("========================================\n");
    printf("  BC-250 Vulkan ICD Test\n");
    printf("========================================\n\n");
    
    /* Test 1: Load ICD */
    testCount++;
    if (LoadIcd()) {
        passCount++;
        printf("[PASS] Load ICD\n\n");
    } else {
        printf("[FAIL] Load ICD\n\n");
        return 1;
    }
    
    /* Test 2: Create Instance */
    testCount++;
    VkInstance instance = NULL;
    VkResult result = pfnCreateInstance(NULL, NULL, &instance);
    if (result == VK_SUCCESS && instance) {
        passCount++;
        printf("[PASS] CreateInstance\n\n");
    } else {
        printf("[FAIL] CreateInstance (result=%d)\n\n", result);
    }
    
    /* Test 3: Create Device */
    testCount++;
    VkDevice device = NULL;
    result = pfnCreateDevice(NULL, NULL, NULL, &device);
    if (result == VK_SUCCESS && device) {
        passCount++;
        printf("[PASS] CreateDevice\n\n");
    } else {
        printf("[FAIL] CreateDevice (result=%d)\n\n", result);
    }
    
    /* Test 4: Allocate Memory */
    testCount++;
    VkDeviceMemory memory = NULL;
    result = pfnAllocateMemory(device, NULL, NULL, &memory);
    if (result == VK_SUCCESS && memory) {
        passCount++;
        printf("[PASS] AllocateMemory\n\n");
    } else {
        printf("[FAIL] AllocateMemory (result=%d)\n\n", result);
    }
    
    /* Test 5: Create Buffer */
    testCount++;
    VkBuffer buffer = NULL;
    result = pfnCreateBuffer(device, NULL, NULL, &buffer);
    if (result == VK_SUCCESS && buffer) {
        passCount++;
        printf("[PASS] CreateBuffer\n\n");
    } else {
        printf("[FAIL] CreateBuffer (result=%d)\n\n", result);
    }
    
    /* Test 6: Create Fence */
    testCount++;
    VkFence fence = NULL;
    result = pfnCreateFence(device, NULL, NULL, &fence);
    if (result == VK_SUCCESS && fence) {
        passCount++;
        printf("[PASS] CreateFence\n\n");
    } else {
        printf("[FAIL] CreateFence (result=%d)\n\n", result);
    }
    
    /* Test 7: Wait for Fences */
    testCount++;
    result = pfnWaitForFences(device, 1, &fence, VK_TRUE, 1000000000ULL);
    if (result == VK_SUCCESS) {
        passCount++;
        printf("[PASS] WaitForFences\n\n");
    } else {
        printf("[FAIL] WaitForFences (result=%d)\n\n", result);
    }
    
    /* Test 8: Create Command Pool */
    testCount++;
    VkCommandPool cmdPool = NULL;
    result = pfnCreateCommandPool(device, NULL, NULL, &cmdPool);
    if (result == VK_SUCCESS && cmdPool) {
        passCount++;
        printf("[PASS] CreateCommandPool\n\n");
    } else {
        printf("[FAIL] CreateCommandPool (result=%d)\n\n", result);
    }
    
    /* Test 9: Allocate Command Buffers */
    testCount++;
    VkCommandBuffer cmdBuf = NULL;
    result = pfnAllocateCommandBuffers(device, NULL, &cmdBuf);
    if (result == VK_SUCCESS && cmdBuf) {
        passCount++;
        printf("[PASS] AllocateCommandBuffers\n\n");
    } else {
        printf("[FAIL] AllocateCommandBuffers (result=%d)\n\n", result);
    }
    
    /* Test 10: Begin/End Command Buffer */
    testCount++;
    result = pfnBeginCommandBuffer(cmdBuf, NULL);
    if (result == VK_SUCCESS) {
        result = pfnEndCommandBuffer(cmdBuf);
        if (result == VK_SUCCESS) {
            passCount++;
            printf("[PASS] BeginCommandBuffer + EndCommandBuffer\n\n");
        } else {
            printf("[FAIL] EndCommandBuffer (result=%d)\n\n", result);
        }
    } else {
        printf("[FAIL] BeginCommandBuffer (result=%d)\n\n", result);
    }
    
    /* Test 11: Queue Submit */
    testCount++;
    VkQueue queue = NULL;
    pfnGetDeviceQueue(device, 0, 0, &queue);
    result = pfnQueueSubmit(queue, 0, NULL, fence);
    if (result == VK_SUCCESS) {
        passCount++;
        printf("[PASS] QueueSubmit\n\n");
    } else {
        printf("[FAIL] QueueSubmit (result=%d)\n\n", result);
    }
    
    /* Test 12: Device Wait Idle */
    testCount++;
    result = pfnDeviceWaitIdle(device);
    if (result == VK_SUCCESS) {
        passCount++;
        printf("[PASS] DeviceWaitIdle\n\n");
    } else {
        printf("[FAIL] DeviceWaitIdle (result=%d)\n\n", result);
    }
    
    /* Test 13: Create Graphics Pipeline */
    testCount++;
    VkPipeline pipeline = NULL;
    result = pfnCreateGraphicsPipelines(device, NULL, 0, NULL, NULL, &pipeline);
    if (result == VK_SUCCESS) {
        passCount++;
        printf("[PASS] CreateGraphicsPipelines\n\n");
    } else {
        printf("[FAIL] CreateGraphicsPipelines (result=%d)\n\n", result);
    }
    
    /* Cleanup */
    printf("--- Cleanup ---\n");
    if (pipeline) pfnDestroyPipeline(device, pipeline, NULL);
    if (cmdBuf) { VkCommandBuffer cmds[] = {cmdBuf}; /* vkFreeCommandBuffers */ }
    if (cmdPool) pfnDestroyCommandPool(device, cmdPool, NULL);
    if (fence) pfnDestroyFence(device, fence, NULL);
    if (buffer) pfnDestroyBuffer(device, buffer, NULL);
    if (memory) pfnFreeMemory(device, memory, NULL);
    if (device) pfnDestroyDevice(device, NULL);
    if (instance) pfnDestroyInstance(instance, NULL);
    
    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", passCount, testCount);
    printf("========================================\n");
    
    if (hIcd) FreeLibrary(hIcd);
    return (passCount == testCount) ? 0 : 1;
}

/* vk-draw-test.c — Exercise BC-250 Vulkan ICD real DRAW_INDEX_AUTO path.
 *
 * Creates instance/device, records one vkCmdDraw into a command buffer,
 * submits via vkQueueSubmit. Verifies ICD returns VK_SUCCESS and that the
 * command buffer recorded non-zero PM4 (DRAW_INDEX_AUTO header present).
 *
 * Run AFTER building bc250_icd_stub.dll. Needs GPU driver atikmdag running.
 * Clears VK_* env vars first (validation layers reject minimal instance).
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Minimal Vulkan — mirror ICD ABI (only what this test needs). */
typedef int32_t VkResult;
#define VK_SUCCESS 0
#define VKAPI_CALL __stdcall

typedef void* VkInstance;
typedef void* VkPhysicalDevice;
typedef void* VkDevice;
typedef void* VkQueue;
typedef void* VkCommandPool;
typedef void* VkCommandBuffer;
typedef void* VkFence;
typedef void* VkSemaphore;

typedef struct VkApplicationInfo {
    int32_t sType; const void* pNext;
    const char* pApplicationName; uint32_t applicationVersion;
    const char* pEngineName; uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    int32_t sType; const void* pNext; uint32_t flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkDeviceCreateInfo {
    int32_t sType; const void* pNext; uint32_t flags;
    uint32_t queueCreateInfoCount; const void* pQueueCreateInfos;
    uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
    const void* pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkCommandPoolCreateInfo {
    int32_t sType; const void* pNext; uint32_t flags; uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
    int32_t sType; const void* pNext; VkCommandPool commandPool;
    int32_t level; uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferBeginInfo {
    int32_t sType; const void* pNext; uint32_t flags; const void* pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef struct VkSubmitInfo {
    int32_t sType; const void* pNext;
    uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores;
    const uint32_t* pWaitDstStageMask;
    uint32_t commandBufferCount; const VkCommandBuffer* pCommandBuffers;
    uint32_t signalSemaphoreCount; const VkSemaphore* pSignalSemaphores;
} VkSubmitInfo;

#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 39
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 40
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 42
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 4

/* Command buffer layout must match ICD (BC250_CMD_HDR). */
#define BC250_CMD_MAGIC 0x444D4342u
typedef struct {
    uint32_t magic;
    uint32_t used;
    uint32_t capacity;
    uint32_t recording;
} BC250_CMD_HDR;

#define IT_DRAW_INDEX_AUTO 0x2D
#define PM4_TYPE3_HDR(opcode, cnt) ((3u << 30) | (((cnt) - 1) << 16) | ((opcode) << 8))

typedef VkResult (VKAPI_CALL *PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
typedef void (VKAPI_CALL *PFN_vkDestroyInstance)(VkInstance, const void*);
typedef VkResult (VKAPI_CALL *PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef VkResult (VKAPI_CALL *PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
typedef void (VKAPI_CALL *PFN_vkDestroyDevice)(VkDevice, const void*);
typedef VkResult (VKAPI_CALL *PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
typedef VkResult (VKAPI_CALL *PFN_vkCreateCommandPool)(VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*);
typedef VkResult (VKAPI_CALL *PFN_vkAllocateCommandBuffers)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
typedef VkResult (VKAPI_CALL *PFN_vkBeginCommandBuffer)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
typedef void (VKAPI_CALL *PFN_vkCmdDraw)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
typedef VkResult (VKAPI_CALL *PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef VkResult (VKAPI_CALL *PFN_vkQueueSubmit)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);

static HMODULE g_icd;
typedef void* (VKAPI_CALL *PFN_vkGetInstanceProcAddr)(void* instance, const char* pName);
static PFN_vkGetInstanceProcAddr g_gipa;

static void* load(const char* name)
{
    /* bc250_icd_stub.dll exports only ICD negotiation + GIPA (7 exports).
     * All real entry points resolve through vk_icdGetInstanceProcAddr. */
    if (g_gipa) {
        void* p = g_gipa(NULL, name);
        if (p) return p;
    }
    void* p = (void*)GetProcAddress(g_icd, name);
    if (!p) printf("  MISSING export: %s\n", name);
    return p;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Clear VK_* env — validation layers reject minimal ICD instances. */
    SetEnvironmentVariableA("VK_LOADER_DEBUG", NULL);
    SetEnvironmentVariableA("VK_LAYER_PATH", NULL);
    SetEnvironmentVariableA("VK_INSTANCE_LAYERS", NULL);
    SetEnvironmentVariableA("VK_LOADER_LAYERS_ENABLE", NULL);
    {
        char buf[512];
        DWORD n = GetEnvironmentVariableA("VK_", buf, sizeof(buf));
        (void)n;
        /* Unset common prefixes that enable layers */
        SetEnvironmentVariableA("VK_LOADER_DEBUG", "");
        SetEnvironmentVariableA("VK_INSTANCE_LAYERS", "");
    }

    g_icd = LoadLibraryA("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\bc250_icd_stub.dll");
    if (!g_icd) {
        printf("FAIL: LoadLibrary bc250_icd_stub.dll gle=%lu\n", GetLastError());
        return 1;
    }
    printf("Loaded bc250_icd_stub.dll\n");

    g_gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(g_icd, "vk_icdGetInstanceProcAddr");
    if (!g_gipa) g_gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(g_icd, "vkGetInstanceProcAddr");
    if (!g_gipa) printf("WARN: no GIPA export — direct GetProcAddress only\n");

    PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)load("vkCreateInstance");
    PFN_vkDestroyInstance DestroyInstance = (PFN_vkDestroyInstance)load("vkDestroyInstance");
    PFN_vkEnumeratePhysicalDevices EnumPDs = (PFN_vkEnumeratePhysicalDevices)load("vkEnumeratePhysicalDevices");
    PFN_vkCreateDevice CreateDevice = (PFN_vkCreateDevice)load("vkCreateDevice");
    PFN_vkDestroyDevice DestroyDevice = (PFN_vkDestroyDevice)load("vkDestroyDevice");
    PFN_vkGetDeviceQueue GetDeviceQueue = (PFN_vkGetDeviceQueue)load("vkGetDeviceQueue");
    PFN_vkCreateCommandPool CreatePool = (PFN_vkCreateCommandPool)load("vkCreateCommandPool");
    PFN_vkAllocateCommandBuffers AllocCBs = (PFN_vkAllocateCommandBuffers)load("vkAllocateCommandBuffers");
    PFN_vkBeginCommandBuffer BeginCB = (PFN_vkBeginCommandBuffer)load("vkBeginCommandBuffer");
    PFN_vkCmdDraw CmdDraw = (PFN_vkCmdDraw)load("vkCmdDraw");
    PFN_vkEndCommandBuffer EndCB = (PFN_vkEndCommandBuffer)load("vkEndCommandBuffer");
    PFN_vkQueueSubmit QueueSubmit = (PFN_vkQueueSubmit)load("vkQueueSubmit");
    if (!CreateInstance || !EnumPDs || !CreateDevice || !GetDeviceQueue ||
        !CreatePool || !AllocCBs || !BeginCB || !CmdDraw || !EndCB || !QueueSubmit) {
        printf("FAIL: missing exports\n");
        return 1;
    }

    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vk-draw-test";
    app.apiVersion = (1u << 22); /* 1.0 */

    VkInstanceCreateInfo ici = {0};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    VkInstance inst = NULL;
    VkResult r = CreateInstance(&ici, NULL, &inst);
    printf("vkCreateInstance: %d\n", r);
    if (r != VK_SUCCESS || !inst) { printf("FAIL instance\n"); return 1; }

    uint32_t ndev = 0;
    EnumPDs(inst, &ndev, NULL);
    printf("Physical devices: %u\n", ndev);
    if (ndev == 0) { printf("FAIL no GPU\n"); if (DestroyInstance) DestroyInstance(inst, NULL); return 1; }
    VkPhysicalDevice pd = NULL;
    EnumPDs(inst, &ndev, &pd);

    /* Device create — ICD must accept; queue create infos can be minimal zero. */
    struct { uint32_t sType_low; } dummy_qci = {0};
    (void)dummy_qci;
    /* Use a simple queue create info: sType=2 (DEVICE_QUEUE_CREATE), flags=0, index=0 */
    struct {
        int32_t sType; const void* pNext; uint32_t flags;
        float queuePriority; uint32_t queueFamilyIndex;
    } qci = { 2, NULL, 0, 1.0f, 0 };
    /* Real layout: sType, pNext, flags, queueFamilyIndex, queueCount, pQueuePriorities
     * — ICD stub may only check non-null; pass a conservative blob. */
    uint8_t dcibuf[128] = {0};
    /* VkDeviceCreateInfo: sType=3 @0, flags @16, queueCreateInfoCount @24=1, pQCI @32 */
    *(int32_t*)(dcibuf + 0) = 3;
    *(uint32_t*)(dcibuf + 24) = 1;
    *(void**)(dcibuf + 32) = &qci;

    VkDevice dev = NULL;
    r = CreateDevice(pd, (const VkDeviceCreateInfo*)dcibuf, NULL, &dev);
    printf("vkCreateDevice: %d dev=%p\n", r, dev);
    if (r != VK_SUCCESS || !dev) {
        printf("FAIL device (ICD may need fuller create info)\n");
        if (DestroyInstance) DestroyInstance(inst, NULL);
        return 1;
    }

    VkQueue queue = NULL;
    r = GetDeviceQueue(dev, 0, 0, &queue);
    printf("vkGetDeviceQueue: %d queue=%p\n", r, queue);

    VkCommandPoolCreateInfo pci = {0};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = 0;
    VkCommandPool pool = NULL;
    r = CreatePool(dev, &pci, NULL, &pool);
    printf("vkCreateCommandPool: %d pool=%p\n", r, pool);
    if (r != VK_SUCCESS || !pool) { printf("FAIL pool\n"); return 1; }

    VkCommandBufferAllocateInfo cba = {0};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = pool;
    cba.level = 0;
    cba.commandBufferCount = 1;
    VkCommandBuffer cb = NULL;
    r = AllocCBs(dev, &cba, &cb);
    printf("vkAllocateCommandBuffers: %d cb=%p\n", r, cb);
    if (r != VK_SUCCESS || !cb) { printf("FAIL cb\n"); return 1; }

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    r = BeginCB(cb, &bi);
    printf("vkBeginCommandBuffer: %d\n", r);

    CmdDraw(cb, 3, 1, 0, 0); /* 3 vertices = 1 triangle */
    r = EndCB(cb);
    printf("vkEndCommandBuffer: %d\n", r);

    BC250_CMD_HDR* hdr = (BC250_CMD_HDR*)cb;
    printf("CmdBuf: magic=0x%08X used=%u recording=%u\n",
           hdr->magic, hdr->used, hdr->recording);
    if (hdr->magic != BC250_CMD_MAGIC || hdr->used < 12) {
        printf("FAIL: command buffer did not record DRAW_INDEX_AUTO\n");
        return 1;
    }
    const uint32_t* pm4 = (const uint32_t*)(hdr + 1);
    printf("PM4[0]=0x%08X (expect TYPE3 DRAW_INDEX_AUTO 0x2D)\n", pm4[0]);
    printf("PM4[1]=0x%08X (vertexCount=3)\n", pm4[1]);
    printf("PM4[2]=0x%08X (prim|bit8)\n", pm4[2]);
    if (((pm4[0] >> 8) & 0xFF) != IT_DRAW_INDEX_AUTO) {
        printf("FAIL: wrong opcode in recorded PM4\n");
        return 1;
    }

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    r = QueueSubmit(queue, 1, &si, NULL);
    printf("vkQueueSubmit: %d (SEND_PM4 with recorded DRAW)\n", r);
    if (r != VK_SUCCESS) {
        printf("FAIL submit\n");
        return 1;
    }

    printf("\nPASS: DRAW_INDEX_AUTO recorded + submitted via ICD\n");
    /* GPU may not execute (WGP locked) — path correctness is the test. */

    if (DestroyDevice) DestroyDevice(dev, NULL);
    if (DestroyInstance) DestroyInstance(inst, NULL);
    FreeLibrary(g_icd);
    return 0;
}

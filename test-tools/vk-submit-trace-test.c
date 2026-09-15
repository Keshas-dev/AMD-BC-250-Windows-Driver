/* vk-submit-trace-test.c - full loader path trace: instance -> device -> queue ->
 * command pool/buffer -> vkQueueSubmit -> vkWaitForFences.
 *
 * Exercises the REAL ICD submit path (src/vulkan/bc250_vulkan_icd.c
 * bc250_vkQueueSubmit -> IOCTL_AMDBC250_SUBMIT_COMMANDS 0x80000880 with
 * NOP 0x80000000 + EOP fence packet). The ICD logs first DWORD + ok/gle
 * via test-tools/wddm_debug_hooks.c (OutputDebugStringA + stdout).
 *
 * Run with: set VK_ICD_FILENAMES=<repo>\\output\\amdbc250_icd.json
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include "..\inc\amdbc250_ioctl.h"
#include "wddm_debug_hooks.c"

static DWORD read_reg(HANDLE h, unsigned off, unsigned *out) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD br = 0;
    r.RegisterOffset = off; r.Value = 0;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_READ_REG,
                         &r, sizeof(r), &r, sizeof(r), &br, NULL))
        return GetLastError();
    *out = r.Value;
    return 0;
}

static const char *rn(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "SUCCESS";
    case VK_TIMEOUT: return "TIMEOUT";
    case VK_NOT_READY: return "NOT_READY";
    case VK_ERROR_DEVICE_LOST: return "ERR_DEVICE_LOST";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "ERR_HOST_MEM";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "ERR_DEVICE_MEM";
    case VK_ERROR_INITIALIZATION_FAILED: return "ERR_INIT";
    default: return "OTHER";
    }
}

int main(int argc, char **argv) {
    /* Default (no args): direct SEND_PM4 trace only (safe).
     * "loader": also run loader vkQueueSubmit (currently AVs in ICD). */
    int run_loader = (argc > 1 && strcmp(argv[1], "loader") == 0);
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 submit trace (mode=%s) ===\n",
           run_loader ? "loader+direct" : "direct");

    if (run_loader) {
    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "BC250 Submit Trace";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&ci, NULL, &inst);
    printf("vkCreateInstance: %s\n", rn(r));
    if (r != VK_SUCCESS) return 1;

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    if (n == 0) { printf("no GPUs\n"); return 1; }
    VkPhysicalDevice *g = (VkPhysicalDevice *)malloc(n * sizeof(*g));
    vkEnumeratePhysicalDevices(inst, &n, g);
    /* Loader may expose several ICDs (e.g. Mesa LVP from registry).
     * Pick ours by device name. */
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p = {0};
        vkGetPhysicalDeviceProperties(g[i], &p);
        printf("GPU[%u]: %s (API %d.%d)\n", i, p.deviceName,
               VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion));
        if (strstr(p.deviceName, "BC-250") && gpu == VK_NULL_HANDLE)
            gpu = g[i];
    }
    free(g);
    if (gpu == VK_NULL_HANDLE) { printf("BC-250 GPU not found\n"); return 1; }

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, NULL);
    printf("queue families: %u\n", qn);
    if (qn == 0) return 1;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = {0};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = 0;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    VkDeviceCreateInfo di = {0};
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;

    VkDevice dev = VK_NULL_HANDLE;
    r = vkCreateDevice(gpu, &di, NULL, &dev);
    printf("vkCreateDevice: %s\n", rn(r));
    if (r != VK_SUCCESS) return 1;

    VkQueue q = VK_NULL_HANDLE;
    vkGetDeviceQueue(dev, 0, 0, &q);
    printf("vkGetDeviceQueue: %p\n", (void *)q);

    VkCommandPoolCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.queueFamilyIndex = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    r = vkCreateCommandPool(dev, &pi, NULL, &pool);
    printf("vkCreateCommandPool: %s\n", rn(r));

    VkCommandBufferAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    r = vkAllocateCommandBuffers(dev, &ai, &cb);
    printf("vkAllocateCommandBuffers: %s\n", rn(r));

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    r = vkBeginCommandBuffer(cb, &bi);
    printf("vkBeginCommandBuffer: %s\n", rn(r));
    r = vkEndCommandBuffer(cb);
    printf("vkEndCommandBuffer: %s\n", rn(r));

    VkFenceCreateInfo fi = {0};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    r = vkCreateFence(dev, &fi, NULL, &fence);
    printf("vkCreateFence: %s\n", rn(r));

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    printf("--- vkQueueSubmit (ICD builds NOP+EOP, first DWORD should be 0x80000000) ---\n");
    r = vkQueueSubmit(q, 1, &si, fence);
    printf("vkQueueSubmit: %s\n", rn(r));

    r = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ULL);
    printf("vkWaitForFences(5s): %s\n", rn(r));
    r = vkGetFenceStatus(dev, fence);
    printf("vkGetFenceStatus: %s\n", rn(r));

    vkDestroyFence(dev, fence, NULL);
    vkFreeCommandBuffers(dev, pool, 1, &cb);
    vkDestroyCommandPool(dev, pool, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    } /* run_loader */

    /* --- Phase 2: direct KMD SEND_PM4 (proven-safe inline channel,
     * 0x80000B84) with NOP + EOP. First DWORD logged via debug hook. --- */
    printf("\n--- direct SEND_PM4 NOP+EOP via KMD ---\n");
    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("KMD open FAILED gle=%lu\n", GetLastError());
    } else {
        AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
        ZeroMemory(&ih, sizeof(ih));
        ih.MmioPhysicalBase = 0xFE800000ULL;
        ih.MmioSize = 0x80000;
        ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
        DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
                        &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);

        unsigned scr0 = 0, wptr0 = 0, rptr0 = 0;
        AMDBC250_IOCTL_HW_STATUS hs;
        ZeroMemory(&hs, sizeof(hs));
        if (DeviceIoControl(h, IOCTL_AMDBC250_GET_HW_STATUS,
                            NULL, 0, &hs, sizeof(hs), &br, NULL)) {
            wptr0 = hs.GfxRingWptr; rptr0 = hs.GfxRingRptr;
        }
        read_reg(h, 0x32D4, &scr0);
        printf("before: SCRATCH=0x%08X WPTR=0x%08X RPTR=0x%08X\n",
               scr0, wptr0, rptr0);

        /* NOP (TYPE2 0x30000000) + EOP fence packet, inline struct. */
        AMDBC250_IOCTL_SEND_PM4 sp;
        ZeroMemory(&sp, sizeof(sp));
        sp.Commands[0] = 0x30000000; /* PM4 TYPE2 NOP <- first DWORD */
        sp.Commands[1] = 0xC0034600; /* IT_EVENT_WRITE_EOP */
        sp.Commands[2] = 0xA0000246;
        sp.Commands[3] = 0x00000000;
        sp.Commands[4] = 0x00000000;
        sp.Commands[5] = 0x00000001;
        sp.Commands[6] = 0x00000000;
        sp.CommandCount = 7;
        sp.FenceValue = 1;
        sp.QueueType = 0; /* GFX */
        /* KMD checks sizeof(struct)+CommandCount*4 <= inputLen (kmd.c:4709),
         * so pad input with 64 extra DWORDs. */
        UCHAR inPad[sizeof(sp) + 64 * sizeof(UINT32)];
        ZeroMemory(inPad, sizeof(inPad));
        memcpy(inPad, &sp, sizeof(sp));
        BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_SEND_PM4,
                                  inPad, sizeof(inPad), NULL, 0, &br, NULL);
        bc250_debug_submit_status(ok, ok ? 0 : GetLastError(),
                                  sp.CommandCount * 4, sp.Commands[0]);

        unsigned scrAfter = 0;
        ZeroMemory(&hs, sizeof(hs));
        if (DeviceIoControl(h, IOCTL_AMDBC250_GET_HW_STATUS,
                            NULL, 0, &hs, sizeof(hs), &br, NULL)) {
            printf("after:  WPTR=0x%08X RPTR=0x%08X (dWPTR=%u, engine %s)\n",
                   hs.GfxRingWptr, hs.GfxRingRptr,
                   hs.GfxRingWptr - wptr0,
                   hs.GfxRingRptr != rptr0 ? "CONSUMING" : "not consuming");
        }
        read_reg(h, 0x32D4, &scrAfter);
        printf("after:  SCRATCH=0x%08X\n", scrAfter);
        CloseHandle(h);
    }

    printf("=== trace done ===\n");
    return 0;
}

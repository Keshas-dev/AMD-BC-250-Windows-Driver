#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"
#define IOCTL_AMDBC250_READ_REG   ((ULONG)0x80000B88)
#define IOCTL_AMDBC250_WRITE_REG  ((ULONG)0x80000B8C)
#define IOCTL_AMDBC250_INIT_HARDWARE ((ULONG)0x80000B80)

typedef struct { ULONG Offset, Value, Status; } REG_IOCTL;
typedef struct { ULONG64 MmioPhysicalBase; ULONG MmioSize, Flags; ULONG64 FbPhysicalBase; ULONG FbSize; } INIT_HW;

static HANDLE g_h;
static int OpenGpu(void) {
    g_h = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    return g_h != INVALID_HANDLE_VALUE ? 0 : -1;
}
static ULONG ReadReg(ULONG offset) {
    REG_IOCTL req = {offset,0,0}; DWORD ret;
    DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL);
    return req.Value;
}
static int WriteReg(ULONG offset, ULONG value) {
    REG_IOCTL req = {offset,value,0}; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL) ? 0 : -1;
}
static int InitHw(void) {
    INIT_HW ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih,sizeof(ih), &ih,sizeof(ih), &ret, NULL) ? 0 : -1;
}

/* GRBM_GFX_INDEX offsets from hw.h */
#define GRBM_GFX_INDEX         0x34D0
#define GRBM_GFX_INDEX_MEID    16
#define GRBM_GFX_INDEX_PIPEID  8
#define GRBM_GFX_INDEX_QUEUEID 0
#define GRBM_GFX_INDEX_SEID    24
#define SE_BROADCAST           (1u << 31)
#define PIPE_BROADCAST         (1u << 29)
#define QUEUE_BROADCAST        (1u << 30)
#define INSTANCE_BROADCAST     (1u << 26)

/* CP_HQD registers at Linux-corrected offsets */
#define CP_MQD_BASE_ADDR       0x9104
#define CP_MQD_BASE_ADDR_HI    0x9108
#define CP_HQD_ACTIVE          0x910C
#define CP_HQD_PQ_BASE         0x9124
#define CP_HQD_PQ_BASE_HI      0x9128
#define CP_HQD_PQ_CONTROL      0x9148
#define CP_HQD_PQ_WPTR_LO      0x91DC
#define CP_HQD_PQ_WPTR_HI      0x91E0
#define CP_HQD_VMID            0x9110
#define CP_HQD_PERSISTENT_STATE 0x9114
#define CP_HQD_PQ_DOORBELL_CONTROL 0x9150
#define CP_HQD_EOP_BASE_ADDR   0x9164
#define CP_HQD_EOP_BASE_ADDR_HI 0x9168
#define CP_HQD_EOP_CONTROL     0x916C
#define CP_MQD_CONTROL          0x9108  /* also known as BASE_ADDR_HI */

static void print_regs(const char *label) {
    printf("%s:\n", label);
    printf("  GRBM_GFX_INDEX(0x34D0)=0x%08X\n", ReadReg(GRBM_GFX_INDEX));
    printf("  CP_HQD_ACTIVE(0x910C)=0x%08X\n", ReadReg(CP_HQD_ACTIVE));
    printf("  CP_HQD_PQ_BASE(0x9124)=0x%08X\n", ReadReg(CP_HQD_PQ_BASE));
    printf("  CP_HQD_PQ_CONTROL(0x9148)=0x%08X\n", ReadReg(CP_HQD_PQ_CONTROL));
    printf("  CP_HQD_VMID(0x9110)=0x%08X\n", ReadReg(CP_HQD_VMID));
    printf("  CP_MQD_BASE(0x9104)=0x%08X\n", ReadReg(CP_MQD_BASE_ADDR));
}

int main(void) {
    if (OpenGpu() != 0) { printf("Cannot open GPU\n"); return 1; }
    if (InitHw() != 0) { printf("InitHw err=%lu\n", GetLastError()); return 1; }
    printf("=== Linux-style KIQ init via CP_HQD ===\n\n");

    /* Step 1: Read baseline (broadcast mode) */
    print_regs("Before (broadcast)");

    /* Step 2: Select ME=1 (MEC), PIPE=0, QUEUE=0 via GRBM_GFX_INDEX */
    ULONG kiq_select = (1 << GRBM_GFX_INDEX_MEID) |  // ME=1 (MEC)
                       (0 << GRBM_GFX_INDEX_PIPEID) | // PIPE=0
                       (0 << GRBM_GFX_INDEX_QUEUEID); // QUEUE=0
    printf("\nSelecting KIQ: GRBM_GFX_INDEX=0x%08X\n", kiq_select);
    WriteReg(GRBM_GFX_INDEX, kiq_select);
    printf("After select: GRBM_GFX_INDEX=0x%08X\n", ReadReg(GRBM_GFX_INDEX));

    /* Step 3: Read CP_HQD registers with MEC selected */
    printf("\n--- CP_HQD under KIQ select (ME=1,PIPE=0,Q=0) ---\n");
    printf("CP_HQD_ACTIVE(0x910C)=0x%08X\n", ReadReg(CP_HQD_ACTIVE));
    printf("CP_HQD_PQ_BASE(0x9124)=0x%08X\n", ReadReg(CP_HQD_PQ_BASE));
    printf("CP_HQD_PQ_CONTROL(0x9148)=0x%08X\n", ReadReg(CP_HQD_PQ_CONTROL));
    printf("CP_HQD_VMID(0x9110)=0x%08X\n", ReadReg(CP_HQD_VMID));
    printf("CP_MQD_BASE(0x9104)=0x%08X\n", ReadReg(CP_MQD_BASE_ADDR));
    printf("CP_MQD_BASE_HI(0x9108)=0x%08X\n", ReadReg(CP_MQD_BASE_ADDR_HI));

    /* Step 4: Try writing CP_HQD_ACTIVE=1 */
    printf("\n--- Writing CP_HQD_ACTIVE=1 ---\n");
    WriteReg(CP_HQD_ACTIVE, 1);
    printf("CP_HQD_ACTIVE after write=0x%08X\n", ReadReg(CP_HQD_ACTIVE));

    /* Step 5: Try writing some test values to see if banked registers are writable */
    printf("\n--- Testing PQ_BASE writability ---\n");
    ULONG before = ReadReg(CP_HQD_PQ_BASE);
    printf("CP_HQD_PQ_BASE before=0x%08X\n", before);
    WriteReg(CP_HQD_PQ_BASE, 0xDEADBEEF);
    ULONG after = ReadReg(CP_HQD_PQ_BASE);
    printf("CP_HQD_PQ_BASE after write 0xDEADBEEF = 0x%08X\n", after);
    if (after == 0xDEADBEEF) printf("  => WRITABLE! (would restore)\n");
    else printf("  => read-only (0x%08X)\n", after);
    /* Restore */
    WriteReg(CP_HQD_PQ_BASE, before);

    /* Step 6: Reset back to broadcast */
    printf("\n--- Reset to broadcast ---\n");
    WriteReg(GRBM_GFX_INDEX, SE_BROADCAST | PIPE_BROADCAST | QUEUE_BROADCAST | INSTANCE_BROADCAST);
    printf("After broadcast: GRBM_GFX_INDEX=0x%08X\n", ReadReg(GRBM_GFX_INDEX));

    /* Step 7: Switch back to GFX mode (ME=0,PIPE=0,Q=0) */
    printf("\n--- Switch to GFX mode (ME=0,PIPE=0,Q=0) ---\n");
    WriteReg(GRBM_GFX_INDEX, 0);
    printf("After GFX select: GRBM_GFX_INDEX=0x%08X\n", ReadReg(GRBM_GFX_INDEX));
    printf("CP_HQD_ACTIVE under GFX select=0x%08X\n", ReadReg(CP_HQD_ACTIVE));

    /* Step 8: Check if RLC_CNTL changes under MEC select */
    printf("\n--- RLC_CNTL under KIQ select ---\n");
    WriteReg(GRBM_GFX_INDEX, kiq_select);
    printf("RLC_CNTL(0x14260)=0x%08X\n", ReadReg(0x14260));
    WriteReg(GRBM_GFX_INDEX, 0);  /* back to GFX */

    CloseHandle(g_h);
    return 0;
}

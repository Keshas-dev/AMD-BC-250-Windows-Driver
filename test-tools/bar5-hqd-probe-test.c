#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}

/* HQD registers (Linux gc_10_1_0_offset.h, BAR5 = GC_BASE(0x1260) + mm*4) */
#define HQD_ACTIVE       0x910C   /* mmCP_HQD_ACTIVE=0x1FAB */
#define HQD_PQ_BASE_LO   0x9124   /* mmCP_HQD_PQ_BASE=0x1FB1 */
#define HQD_PQ_BASE_HI   0x9128   /* mmCP_HQD_PQ_BASE_HI=0x1FB2 */
#define HQD_PQ_WPTR_LO   0x91DC   /* mmCP_HQD_PQ_WPTR_LO=0x1FDF */
#define HQD_PQ_WPTR_HI   0x91E0   /* mmCP_HQD_PQ_WPTR_HI=0x1FE0 */
#define HQD_PQ_CONTROL   0x9148   /* mmCP_HQD_PQ_CONTROL=0x1FBA */
#define HQD_PQ_RPTR      0x912C   /* mmCP_HQD_PQ_RPTR=0x1FB3 */
#define HQD_QUEUE_PRIORITY 0x911C /* mmCP_HQD_QUEUE_PRIORITY=0x1FAF */
#define HQD_VMID         0x9110   /* mmCP_HQD_VMID=0x1FAC */
#define HQD_PQ_DOORBELL_CONTROL 0x9150 /* mmCP_HQD_PQ_DOORBELL_CONTROL=0x1FBC */
#define HQD_DEQUEUE_REQUEST 0x91BC /* mmCP_HQD_DEQUEUE_REQUEST=0x1FEF */

static void TestWritable(const char* name, uint32_t addr, uint32_t val) {
    uint32_t before = ReadReg(addr);
    WriteReg(addr, val);
    uint32_t after = ReadReg(addr);
    WriteReg(addr, before);
    const char* verdict = (after == val) ? "WRITABLE!" :
                          (after == before) ? "RO" : "PARTIAL/W1C";
    printf("  %-22s 0x%04X: 0x%08X -> 0x%08X -> 0x%08X  [%s]\n",
           name, addr, before, val, after, verdict);
}

int main(void) {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }

    printf("=== INIT_HARDWARE (BAR5 + NBIO_MAP) ===\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
        memset(&ih, 0, sizeof(ih));
        ih.MmioPhysicalBase = 0xFE800000ULL;
        ih.MmioSize = 0x80000;
        ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
        if (!DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), NULL, 0, &br, NULL)) {
            printf("FAIL: INIT_HARDWARE (err=%lu)\n", GetLastError());
            return 1;
        }
        printf("OK\n");
    }

    printf("\n=== HQD Register Probe (Linux compute/GFX ring path) ===\n");
    TestWritable("HQD_ACTIVE",      HQD_ACTIVE,      0x00000001);
    TestWritable("HQD_PQ_BASE_LO",  HQD_PQ_BASE_LO,  0x12345000);
    TestWritable("HQD_PQ_BASE_HI",  HQD_PQ_BASE_HI,  0x00000000);
    TestWritable("HQD_PQ_WPTR_LO",  HQD_PQ_WPTR_LO,  0x00000008);
    TestWritable("HQD_PQ_WPTR_HI",  HQD_PQ_WPTR_HI,  0x00000000);
    TestWritable("HQD_PQ_CONTROL",  HQD_PQ_CONTROL,  0x00010000);
    TestWritable("HQD_PQ_RPTR",     HQD_PQ_RPTR,     0x00000000);
    TestWritable("HQD_QUEUE_PRIORITY", HQD_QUEUE_PRIORITY, 0x00000001);
    TestWritable("HQD_VMID",        HQD_VMID,        0x00000000);
    TestWritable("HQD_PQ_DOORBELL_CONTROL", HQD_PQ_DOORBELL_CONTROL, 0x00000001);
    TestWritable("HQD_DEQUEUE_REQUEST", HQD_DEQUEUE_REQUEST, 0x00000001);

    printf("\n=== DONE ===\n");
    CloseHandle(g_hDev);
    return 0;
}

#include <windows.h>
#include <stdio.h>
#pragma pack(push, 1)
typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
#pragma pack(pop)
static HANDLE gH;

static void WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096 + 12];
    ((UINT32*)buf)[0] = (UINT32)(pa & 0xFFFFFFFF);
    ((UINT32*)buf)[1] = (UINT32)(pa >> 32);
    ((UINT32*)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C10, buf, 12 + size, NULL, 0, &br, NULL);
}
static ULONG ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inbuf[24], outbuf[4096];
    ((UINT32*)inbuf)[0] = (UINT32)(pa & 0xFFFFFFFF);
    ((UINT32*)inbuf)[1] = (UINT32)(pa >> 32);
    ((UINT32*)inbuf)[2] = size;
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C14, inbuf, 12, outbuf, sizeof(outbuf), &br, NULL);
    if (br > 0) memcpy(data, outbuf, min(br, size));
    return br;
}

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("FAIL: open\n"); return 1; }

    /* Init HW */
    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    /* Test write/read to VRAM at 0xC0000000 */
    printf("=== VRAM Write/Read Test ===\n");
    UINT32 testVal = 0xDEADBEEF;
    WritePhys(0xC0000000ULL, &testVal, 4);

    UINT32 readBack = 0;
    ReadPhys(0xC0000000ULL, &readBack, 4);
    printf("Write 0x%08X to 0xC0000000, read 0x%08X %s\n",
        testVal, readBack, (readBack == testVal) ? "MATCH!" : "MISMATCH");

    /* Test system RAM at 0x7E512000 */
    printf("\n=== System RAM Write/Read Test ===\n");
    WritePhys(0x7E512000ULL, &testVal, 4);
    readBack = 0;
    ReadPhys(0x7E512000ULL, &readBack, 4);
    printf("Write 0x%08X to 0x7E512000, read 0x%08X %s\n",
        testVal, readBack, (readBack == testVal) ? "MATCH!" : "MISMATCH");

    /* Test different system RAM address (low, likely valid) */
    printf("\n=== Low System RAM Write/Read Test ===\n");
    WritePhys(0x10000ULL, &testVal, 4);
    readBack = 0;
    ReadPhys(0x10000ULL, &readBack, 4);
    printf("Write 0x%08X to 0x10000, read 0x%08X %s\n",
        testVal, readBack, (readBack == testVal) ? "MATCH!" : "MISMATCH");

    CloseHandle(gH);
    printf("\nDone\n");
    return 0;
}

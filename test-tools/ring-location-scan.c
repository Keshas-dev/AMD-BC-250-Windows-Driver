#include <windows.h>
#include <stdio.h>
#pragma pack(push, 1)
typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
#pragma pack(pop)
static HANDLE gH;

static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B88, &in, 8, &out, 8, &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B8C, &in, 8, &out, 8, &br, NULL);
}
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

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    printf("=== Ring Location Scan ===\n\n");

    /* 1. Check COMPUTE ring registers (0xDB60-0xDB78) */
    printf("1. COMPUTE ring registers (0xDB60-0xDB78):\n");
    for (UINT32 a = 0xDB60; a <= 0xDB80; a += 4) {
        UINT32 v = R(a);
        W(a, 0xA5A5A5A5);
        UINT32 v2 = R(a);
        W(a, v);
        printf("   0x%04X = 0x%08X %s\n", a, v,
            (v2 != v && v2 != 0xFFFFFFFF) ? "(writable)" :
            (v == 0xFFFFFFFF) ? "(dead)" : "(ro)");
    }

    /* 2. Check KIQ registers (0xE060-0xE080) */
    printf("\n2. KIQ registers (0xE060-0xE080):\n");
    for (UINT32 a = 0xE060; a <= 0xE084; a += 4) {
        UINT32 v = R(a);
        W(a, 0xA5A5A5A5);
        UINT32 v2 = R(a);
        W(a, v);
        printf("   0x%04X = 0x%08X %s\n", a, v,
            (v2 != v && v2 != 0xFFFFFFFF) ? "(writable)" :
            (v == 0xFFFFFFFF) ? "(dead)" : "(ro)");
    }

    /* 3. Scan VRAM for PM4 type-3 headers (bits 31:30 = 11) */
    printf("\n3. VRAM PM4 scan (0x00000000-0x02000000, 1MB steps):\n");
    int pm4Found = 0;
    for (UINT64 pa = 0xC0000000ULL; pa < 0xC2000000ULL; pa += 0x100000) {
        UINT32 tmp[4];
        ReadPhys(pa, tmp, 16);
        if ((tmp[0] >> 30) == 3) {
            printf("   0x%08llX: %08X %08X %08X %08X (PM4!)\n",
                pa, tmp[0], tmp[1], tmp[2], tmp[3]);
            pm4Found = 1;
        }
    }
    if (!pm4Found) printf("   (none found)\n");

    /* 4. Scan system RAM for PM4 patterns (0x6000-0x10000, 4KB steps) */
    printf("\n4. System RAM PM4 scan (0x6000-0x100000, 4KB steps):\n");
    pm4Found = 0;
    for (UINT64 pa = 0x6000ULL; pa < 0x100000ULL; pa += 0x1000) {
        UINT32 tmp[4];
        ReadPhys(pa, tmp, 16);
        if ((tmp[0] >> 30) == 3) {
            printf("   0x%08llX: %08X %08X %08X %08X (PM4!)\n",
                pa, tmp[0], tmp[1], tmp[2], tmp[3]);
            pm4Found = 1;
        }
    }
    if (!pm4Found) printf("   (none found)\n");

    /* 5. Check RLC_CP_SCHEDULERS and related */
    printf("\n5. RLC scheduler area (0xECA0-0xECB0):\n");
    for (UINT32 a = 0xECA0; a <= 0xECB0; a += 4) {
        if (a == 0xECA1) continue; /* non-aligned, skip */
        UINT32 v = R(a);
        printf("   0x%04X = 0x%08X\n", a, v);
    }

    /* 6. Check doorbell BAR registers */
    printf("\n6. Search for ring-related registers (0xDA00-0xDC00) non-zero:\n");
    int found = 0;
    for (UINT32 a = 0xDA00; a <= 0xDC00; a += 4) {
        UINT32 v = R(a);
        if (v != 0 && v != 0xFFFFFFFF) {
            W(a, 0xA5A5A5A5);
            UINT32 v2 = R(a);
            W(a, v);
            printf("   0x%04X = 0x%08X %s\n", a, v,
                (v2 != v) ? "(writable)" : "");
            found++;
        }
    }
    printf("   (%d non-zero registers found)\n", found);

    /* 7. Check first 64K of VRAM in detail */
    printf("\n7. VRAM first 64K (0xC0000000-0xC0010000) detail:\n");
    for (UINT64 pa = 0xC0000000ULL; pa < 0xC0010000ULL; pa += 0x200) {
        UINT32 tmp[4];
        ReadPhys(pa, tmp, 16);
        int allSame = (tmp[0]==tmp[1] && tmp[1]==tmp[2] && tmp[2]==tmp[3]);
        if (tmp[0] != 0xFF070412 && !allSame) {
            printf("   0x%08llX: %08X %08X %08X %08X\n",
                pa, tmp[0], tmp[1], tmp[2], tmp[3]);
        }
    }

    CloseHandle(gH);
    printf("\nDone\n");
    return 0;
}

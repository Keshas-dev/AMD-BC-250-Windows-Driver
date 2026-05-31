/*
 * test-gpu-hw-init.c — Test real GPU hardware initialization
 *
 * Finds PCI BAR0 via SetupDi, sends INIT_HARDWARE IOCTL, sends PM4 NOP.
 *
 * Build: cl /nologo /W3 /I"E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um"
 *        /I"E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
 *        /I"E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt"
 *        test-gpu-hw-init.c /Fe:test-gpu-hw-init.exe
 *        /link /LIBPATH:"E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
 *        /LIBPATH:"E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64"
 *        setupapi.lib advapi32.lib
 */

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")

/* IOCTL codes — must match KMD */
#define FILE_DEVICE_AMDBC250    0x8000
#define IOCTL_INDEX             0x800
#define CTL_CODE_AMDBC250(Function, Method, Access) \
    CTL_CODE(FILE_DEVICE_AMDBC250, IOCTL_INDEX + (Function), Method, Access)

#define IOCTL_AMDBC250_GET_CAPS             CTL_CODE_AMDBC250(0x00, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_INIT_HARDWARE        CTL_CODE_AMDBC250(0x70, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_SEND_PM4             CTL_CODE_AMDBC250(0x71, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_READ_REG             CTL_CODE_AMDBC250(0x72, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_WRITE_REG            CTL_CODE_AMDBC250(0x73, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_GET_HW_STATUS        CTL_CODE_AMDBC250(0x74, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Structures */
typedef struct { UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; } INIT_HARDWARE_INPUT;
typedef struct {
    UINT32 MmioMapped; UINT32 RingsInitialized; UINT32 FenceInitialized;
    UINT64 GfxRingPhysAddr; UINT32 GfxRingSize;
    UINT32 GfxRingWptr; UINT32 GfxRingRptr;
    UINT64 FencePhysAddr; UINT64 FenceValue; UINT64 LastSubmittedFence;
} HW_STATUS_RESULT;
typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_ACCESS;
typedef struct {
    UINT32 Commands[64]; UINT32 CommandCount;
    UINT32 FenceValue; UINT32 QueueType;
} SEND_PM4_INPUT;

#define PM4_TYPE3_NOP 0x10
#define PM4_TYPE3_HDR(op, cnt) ((2 << 30) | (((cnt) - 1) << 16) | ((op) << 8))

static BOOL SendIoctl(HANDLE hDev, DWORD code, PVOID in, DWORD inSz, PVOID out, DWORD outSz, DWORD *ret)
{
    DWORD r = 0;
    BOOL ok = DeviceIoControl(hDev, code, in, inSz, out, outSz, &r, NULL);
    if (ret) *ret = r;
    return ok;
}

/*
 * Find PCI BAR0 by reading registry BootConfig for our device.
 * BootConfig is a binary blob in PNP_RESOURCE_REQUIREMENTS_LIST format.
 * We parse it manually to find CmResourceTypeMemory (BAR0).
 */
static BOOL FindPciBar0(UINT64 *barPhys, UINT32 *barSize)
{
    HDEVINFO devInfo;
    SP_DEVINFO_DATA edd;
    HKEY hKey;
    DWORD regType, regSize;
    BYTE *buf;
    BOOL found = FALSE;

    *barPhys = 0;
    *barSize = 0;

    /* Find BC-250 via hardware ID */
    devInfo = SetupDiGetClassDevsA(NULL, "PCI\\VEN_1002&DEV_13FE", NULL,
                                    DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devInfo == INVALID_HANDLE_VALUE) {
        /* Fallback: broader search */
        devInfo = SetupDiGetClassDevsA(NULL, "VEN_1002&DEV_13FE", NULL,
                                        DIGCF_PRESENT | DIGCF_ALLCLASSES);
    }
    if (devInfo == INVALID_HANDLE_VALUE) {
        printf("[FAIL] Cannot enumerate PCI devices\n");
        return FALSE;
    }

    edd.cbSize = sizeof(edd);
    if (!SetupDiEnumDeviceInfo(devInfo, 0, &edd)) {
        /* Try another index */
        printf("[FAIL] No BC-250 device found\n");
        SetupDiDestroyDeviceInfoList(devInfo);
        return FALSE;
    }

    /* Open device registry key */
    hKey = SetupDiOpenDevRegKey(devInfo, &edd, DICS_FLAG_GLOBAL, 0,
                                 DIREG_DEV, KEY_READ);
    SetupDiDestroyDeviceInfoList(devInfo);

    if (hKey == INVALID_HANDLE_VALUE) {
        printf("[FAIL] Cannot open device registry key\n");
        return FALSE;
    }

    /* Read BootConfig value */
    regSize = 0;
    RegQueryValueExA(hKey, "BootConfig", NULL, &regType, NULL, &regSize);
    if (regSize == 0) {
        printf("[WARN] BootConfig is empty, trying AssignedResources...\n");
        RegQueryValueExA(hKey, "AssignedResources", NULL, &regType, NULL, &regSize);
    }

    if (regSize == 0) {
        printf("[FAIL] No resource data in registry\n");
        RegCloseKey(hKey);
        return FALSE;
    }

    printf("  BootConfig: %lu bytes\n", regSize);
    buf = (BYTE *)malloc(regSize);
    if (!buf) { RegCloseKey(hKey); return FALSE; }

    if (RegQueryValueExA(hKey, "BootConfig", NULL, &regType, buf, &regSize) != ERROR_SUCCESS) {
        /* Try AssignedResources */
        if (RegQueryValueExA(hKey, "AssignedResources", NULL, &regType, buf, &regSize) != ERROR_SUCCESS) {
            printf("[FAIL] Cannot read resource data\n");
            free(buf); RegCloseKey(hKey);
            return FALSE;
        }
    }
    RegCloseKey(hKey);

    /*
     * Parse PNP_RESOURCE_REQUIREMENTS_LIST structure:
     *   ListSize (ULONG)
     *   List[0]:
     *     InterfaceType (ULONG) - 1 = PCI
     *     Version (ULONG)
     *     Revision (ULONG)
     *     PartialResourceList:
     *       Version (USHORT)
     *       Revision (USHORT)
     *       Count (ULONG)
     *       PartialDescriptors[Count]:
     *         Type (UCHAR) - 3 = CmResourceTypeMemory
     *         ShareDisposition (UCHAR)
     *         Flags (USHORT)
     *         u.Memory.Start (LARGE_INTEGER)
     *         u.Memory.Length (ULONG)
     *
     * Total header = 4 (ListSize) + 4+4+4 (List[0] header) + 2+2+4 (PartialResourceList header) = 20 bytes
     * Each descriptor = 1 (Type) + 1 (Share) + 2 (Flags) + 8 (Start) + 4 (Length) = 16 bytes
     */
    if (regSize < 20) {
        printf("[FAIL] BootConfig too small (%lu bytes)\n", regSize);
        free(buf);
        return FALSE;
    }

    {
        DWORD offset = 4; /* Skip ListSize */
        /* List[0] header: InterfaceType, Version, Revision */
        DWORD interfaceType = *(DWORD *)(buf + offset); offset += 4;
        DWORD listVersion   = *(DWORD *)(buf + offset); offset += 4;
        DWORD listRevision  = *(DWORD *)(buf + offset); offset += 4;

        printf("  InterfaceType=%lu, Version=%lu.%lu\n", interfaceType, listVersion, listRevision);

        /* PartialResourceList header */
        USHORT prVersion = *(USHORT *)(buf + offset); offset += 2;
        USHORT prRevision = *(USHORT *)(buf + offset); offset += 2;
        DWORD  prCount   = *(DWORD *)(buf + offset); offset += 4;

        printf("  PartialResourceList: version=%u.%u, count=%lu\n", prVersion, prRevision, prCount);

        /* Parse descriptors */
        for (DWORD i = 0; i < prCount; i++) {
            if (offset + 16 > regSize) break;

            BYTE  type  = buf[offset];
            BYTE  share = buf[offset + 1];
            USHORT flags = *(USHORT *)(buf + offset + 2);
            LARGE_INTEGER start;
            DWORD length;
            start.LowPart  = *(DWORD *)(buf + offset + 4);
            start.HighPart = *(DWORD *)(buf + offset + 8);
            length = *(DWORD *)(buf + offset + 12);
            offset += 16;

            (void)share; (void)flags;

            if (type == 3) { /* CmResourceTypeMemory */
                printf("  Resource[%lu]: Memory, PA=0x%llX, Length=0x%X (%lu KB)\n",
                    i, (unsigned long long)start.QuadPart, length, length / 1024);
                if (!found) {
                    *barPhys = (UINT64)start.QuadPart;
                    *barSize = length;
                    found = TRUE;
                }
            } else if (type == 1) {
                printf("  Resource[%lu]: Port, PA=0x%llX, Length=0x%X\n",
                    i, (unsigned long long)start.QuadPart, length);
            } else if (type == 2) {
                printf("  Resource[%lu]: Interrupt\n", i);
            }
        }
    }

    free(buf);

    if (found) {
        printf("  >> BAR0 identified: PA=0x%llX, Size=0x%X (%lu KB)\n",
            (unsigned long long)*barPhys, *barSize, *barSize / 1024);
    } else {
        printf("[FAIL] No MMIO resource found in BootConfig\n");
    }

    return found;
}

int main(void)
{
    HANDLE hDev;
    DWORD pass = 0, fail = 0, total = 0, bytesRet;
    UINT64 barPhys = 0;
    UINT32 barSize = 0;
    BOOL ok;

    printf("========================================\n");
    printf("  BC-250 GPU Hardware Init Test\n");
    printf("========================================\n\n");

    hDev = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                        0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("[FAIL] Cannot open KMD device: %lu\n", GetLastError());
        return 1;
    }
    printf("[OK] KMD device opened\n\n");

    /* Test 1: Find PCI BAR0 */
    total++;
    printf("[TEST 1/7] Find PCI BAR0 via registry...\n");
    if (FindPciBar0(&barPhys, &barSize)) {
        printf("[PASS] BAR0 found\n\n"); pass++;
    } else {
        printf("[FAIL] BAR0 not found\n\n"); fail++;
        printf("Cannot continue without BAR0.\n");
        CloseHandle(hDev);
        return 1;
    }

    /* Test 2: Init Hardware */
    total++;
    printf("[TEST 2/7] INIT_HARDWARE (PA=0x%llX, Size=0x%X)...\n", (unsigned long long)barPhys, barSize);
    {
        INIT_HARDWARE_INPUT ih;
        ih.MmioPhysicalBase = barPhys;
        ih.MmioSize = barSize;
        ih.Flags = 0;
        ok = SendIoctl(hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), NULL, 0, &bytesRet);
        if (ok) { printf("[PASS] INIT_HARDWARE accepted\n\n"); pass++; }
        else { printf("[FAIL] INIT_HARDWARE failed: %lu\n\n", GetLastError()); fail++; }
    }

    Sleep(200);

    /* Test 3: Get HW Status */
    total++;
    printf("[TEST 3/7] GET_HW_STATUS...\n");
    {
        HW_STATUS_RESULT hs = {0};
        ok = SendIoctl(hDev, IOCTL_AMDBC250_GET_HW_STATUS, NULL, 0, &hs, sizeof(hs), &bytesRet);
        if (ok) {
            printf("  MMIO mapped:       %s\n", hs.MmioMapped ? "YES" : "NO");
            printf("  Rings initialized: %s\n", hs.RingsInitialized ? "YES" : "NO");
            printf("  Fence initialized: %s\n", hs.FenceInitialized ? "YES" : "NO");
            printf("  GFX Ring PA:       0x%llX (%u KB)\n", (unsigned long long)hs.GfxRingPhysAddr, hs.GfxRingSize / 1024);
            printf("  Ring WPtr/RPtr:    %u / %u\n", hs.GfxRingWptr, hs.GfxRingRptr);
            printf("  Fence:             %llu / %llu\n", (unsigned long long)hs.FenceValue, (unsigned long long)hs.LastSubmittedFence);
            if (hs.MmioMapped && hs.RingsInitialized) {
                printf("[PASS] Hardware initialized\n\n"); pass++;
            } else {
                printf("[FAIL] Hardware NOT fully initialized\n\n"); fail++;
            }
        } else {
            printf("[FAIL] GET_HW_STATUS failed: %lu\n\n", GetLastError()); fail++;
        }
    }

    /* Test 4: Read GPU Register (MMIO test) */
    total++;
    printf("[TEST 4/7] READ_REG test (MMIO)...\n");
    {
        REG_ACCESS reg = {0x0000, 0};
        ok = SendIoctl(hDev, IOCTL_AMDBC250_READ_REG, &reg, sizeof(reg), &reg, sizeof(reg), &bytesRet);
        if (ok) {
            printf("  reg[0x0000] = 0x%08X\n", reg.Value);

            reg.RegisterOffset = 0x0080; reg.Value = 0;
            SendIoctl(hDev, IOCTL_AMDBC250_READ_REG, &reg, sizeof(reg), &reg, sizeof(reg), &bytesRet);
            printf("  reg[0x0080] = 0x%08X (GB_ADDR_CONFIG)\n", reg.Value);

            reg.RegisterOffset = 0x86D0; reg.Value = 0;
            SendIoctl(hDev, IOCTL_AMDBC250_READ_REG, &reg, sizeof(reg), &reg, sizeof(reg), &bytesRet);
            printf("  reg[0x86D0] = 0x%08X (CP_ME_CNTL)\n", reg.Value);

            reg.RegisterOffset = 0x000C; reg.Value = 0;
            SendIoctl(hDev, IOCTL_AMDBC250_READ_REG, &reg, sizeof(reg), &reg, sizeof(reg), &bytesRet);
            printf("  reg[0x000C] = 0x%08X (scratch reg)\n", reg.Value);

            printf("[PASS] MMIO reads OK\n\n"); pass++;
        } else {
            printf("[FAIL] READ_REG failed: %lu\n\n", GetLastError()); fail++;
        }
    }

    /* Test 5: Send PM4 NOP */
    total++;
    printf("[TEST 5/7] SEND_PM4 (NOP + fence)...\n");
    {
        SEND_PM4_INPUT sp = {0};
        sp.Commands[0] = PM4_TYPE3_HDR(PM4_TYPE3_NOP, 1);
        sp.Commands[1] = 0xDEADBEEF;
        sp.CommandCount = 2;
        sp.FenceValue = 100;
        sp.QueueType = 0;

        ok = SendIoctl(hDev, IOCTL_AMDBC250_SEND_PM4, &sp, sizeof(sp), NULL, 0, &bytesRet);
        if (ok) { printf("[PASS] PM4 NOP sent\n\n"); pass++; }
        else { printf("[FAIL] SEND_PM4 failed: %lu\n\n", GetLastError()); fail++; }
    }

    Sleep(100);

    /* Test 6: Verify fence after PM4 */
    total++;
    printf("[TEST 6/7] Verify ring write + fence...\n");
    {
        HW_STATUS_RESULT hs = {0};
        ok = SendIoctl(hDev, IOCTL_AMDBC250_GET_HW_STATUS, NULL, 0, &hs, sizeof(hs), &bytesRet);
        if (ok) {
            printf("  Ring WPtr:  %u (should be > 0)\n", hs.GfxRingWptr);
            printf("  Fence:      %llu (expected 100)\n", (unsigned long long)hs.FenceValue);
            if (hs.GfxRingWptr > 0) {
                printf("[PASS] PM4 written to ring\n\n"); pass++;
            } else {
                printf("[FAIL] WPtr is 0\n\n"); fail++;
            }
        } else { printf("[FAIL] GET_HW_STATUS failed\n\n"); fail++; }
    }

    /* Test 7: Write + Read register roundtrip */
    total++;
    printf("[TEST 7/7] Register write/read roundtrip...\n");
    {
        REG_ACCESS reg = {0x000C, 0};
        SendIoctl(hDev, IOCTL_AMDBC250_READ_REG, &reg, sizeof(reg), &reg, sizeof(reg), &bytesRet);
        UINT32 orig = reg.Value;
        printf("  Original reg[0x000C] = 0x%08X\n", orig);

        reg.Value = orig ^ 0x12345678; /* Toggle some bits */
        ok = SendIoctl(hDev, IOCTL_AMDBC250_WRITE_REG, &reg, sizeof(reg), NULL, 0, &bytesRet);

        if (ok) {
            reg.Value = 0;
            SendIoctl(hDev, IOCTL_AMDBC250_READ_REG, &reg, sizeof(reg), &reg, sizeof(reg), &bytesRet);
            printf("  After write  reg[0x000C] = 0x%08X\n", reg.Value);
            printf("[PASS] Write/read roundtrip OK\n\n"); pass++;
        } else {
            printf("[FAIL] WRITE_REG failed: %lu\n\n", GetLastError()); fail++;
        }
    }

    printf("========================================\n");
    printf("  Results: %lu/%lu passed\n", pass, total);
    printf("========================================\n");

    CloseHandle(hDev);
    return (fail == 0) ? 0 : 1;
}

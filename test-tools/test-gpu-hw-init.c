/*
 * test-gpu-hw-init.c — Test real GPU hardware initialization
 *
 * Finds PCI BAR0 by enumerating registry, sends INIT_HARDWARE IOCTL, sends PM4 NOP.
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
#include <string.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")

/* IOCTL codes — must match KMD */
#define FILE_DEVICE_AMDBC250    0x8000
#define IOCTL_INDEX             0x800
#define CTL_CODE_AMDBC250(Function, Method, Access) \
    CTL_CODE(FILE_DEVICE_AMDBC250, IOCTL_INDEX + (Function), Method, Access)

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
 * Parse BootConfig/AssignedResources binary blob to find MMIO BAR0.
 * Format: PNP_RESOURCE_REQUIREMENTS_LIST (manually defined for user-mode)
 */
static BOOL ParseResourceList(BYTE *buf, DWORD bufSize, UINT64 *barPhys, UINT32 *barSize)
{
    DWORD offset;
    DWORD prCount, i;
    BOOL found = FALSE;

    *barPhys = 0;
    *barSize = 0;

    /* PNP_RESOURCE_REQUIREMENTS_LIST layout:
     *   [0..3]  ListSize (ULONG)
     *   [4..7]  InterfaceType (ULONG) = 1 for PCI
     *   [8..11] Version (ULONG)
     *   [12..15] Revision (ULONG)
     *   [16..17] PartialResourceList.Version (USHORT)
     *   [18..19] PartialResourceList.Revision (USHORT)
     *   [20..23] PartialResourceList.Count (ULONG)
     *   [24..]  PartialDescriptors[] (each 16 bytes):
     *     +0:  Type (UCHAR) — 3=CmResourceTypeMemory
     *     +1:  ShareDisposition (UCHAR)
     *     +2:  Flags (USHORT)
     *     +4:  Start (LARGE_INTEGER, 8 bytes)
     *     +12: Length (ULONG)
     */
    if (bufSize < 24) return FALSE;

    /* Skip ListSize + InterfaceType + Version + Revision + PR header */
    offset = 24;
    prCount = *(DWORD *)(buf + 20);

    printf("  Resource count: %lu\n", prCount);

    for (i = 0; i < prCount; i++) {
        BYTE type;
        ULONGLONG start;
        DWORD length;

        if (offset + 16 > bufSize) break;

        type = buf[offset];
        start = 0;
        memcpy(&start, buf + offset + 4, 8);
        length = *(DWORD *)(buf + offset + 12);
        offset += 16;

        if (type == 3) { /* CmResourceTypeMemory */
            printf("  Resource[%lu]: Memory, PA=0x%llX, Length=0x%X (%lu KB)\n",
                i, start, length, length / 1024);
            if (!found && length >= 0x10000) { /* BAR0 should be at least 64KB */
                *barPhys = (UINT64)start;
                *barSize = length;
                found = TRUE;
            }
        } else if (type == 1) {
            printf("  Resource[%lu]: Port, PA=0x%llX, Length=0x%X\n", i, start, length);
        } else if (type == 2) {
            printf("  Resource[%lu]: Interrupt\n", i);
        }
    }
    return found;
}

/*
 * Find PCI BAR0 by enumerating the PCI registry enum key.
 */
static BOOL FindPciBar0(UINT64 *barPhys, UINT32 *barSize)
{
    HKEY hPciKey, hDevKey;
    DWORD index = 0;
    WCHAR subKeyName[256];
    DWORD subKeySize;
    BOOL found = FALSE;

    *barPhys = 0;
    *barSize = 0;

    /* Open HKLM\SYSTEM\CurrentControlSet\Enum\PCI */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Enum\\PCI",
        0, KEY_READ, &hPciKey) != ERROR_SUCCESS) {
        printf("[FAIL] Cannot open PCI enum key\n");
        return FALSE;
    }

    /* Enumerate PCI device IDs — look for VEN_1002&DEV_13FE */
    while (1) {
        subKeySize = sizeof(subKeyName) / sizeof(WCHAR);
        if (RegEnumKeyExW(hPciKey, index++, subKeyName, &subKeySize,
                          NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        /* Check if this is our device */
        if (wcsstr(subKeyName, L"VEN_1002") && wcsstr(subKeyName, L"DEV_13FE")) {
            WCHAR fullSubKey[512];
            DWORD instanceIndex = 0;
            WCHAR instSubKey[256];
            DWORD instSubKeySize;

            printf("  Found PCI device: %ls\n", subKeyName);

            /* Open this device ID key to enumerate instances */
            _snwprintf_s(fullSubKey, sizeof(fullSubKey)/sizeof(WCHAR), _TRUNCATE,
                L"SYSTEM\\CurrentControlSet\\Enum\\PCI\\%s", subKeyName);

            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, fullSubKey, 0, KEY_READ, &hDevKey) != ERROR_SUCCESS) {
                printf("    Cannot open: %ls\n", fullSubKey);
                continue;
            }

            /* Enumerate instances (e.g., 5&12345678&0&00000000) */
            while (1) {
                instSubKeySize = sizeof(instSubKey) / sizeof(WCHAR);
                if (RegEnumKeyExW(hDevKey, instanceIndex++, instSubKey, &instSubKeySize,
                                  NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;

                WCHAR instFullKey[1024];
                _snwprintf_s(instFullKey, sizeof(instFullKey)/sizeof(WCHAR), _TRUNCATE,
                    L"SYSTEM\\CurrentControlSet\\Enum\\PCI\\%s\\%s",
                    subKeyName, instSubKey);

                printf("    Instance: %ls\n", instSubKey);

                /* Try to read BootConfig from this instance */
                {
                    HKEY hInstKey;
                    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, instFullKey, 0, KEY_READ, &hInstKey) == ERROR_SUCCESS) {
                        BYTE regData[8192];
                        DWORD regSize = sizeof(regData);
                        DWORD regType = 0;

                        /* Try BootConfig first */
                        if (RegQueryValueExW(hInstKey, L"BootConfig", NULL, &regType, regData, &regSize) == ERROR_SUCCESS
                            && regSize > 24) {
                            printf("    BootConfig: %lu bytes\n", regSize);
                            found = ParseResourceList(regData, regSize, barPhys, barSize);
                        }

                        /* Try AssignedResources if BootConfig failed */
                        if (!found) {
                            regSize = sizeof(regData);
                            if (RegQueryValueExW(hInstKey, L"AssignedResources", NULL, &regType, regData, &regSize) == ERROR_SUCCESS
                                && regSize > 24) {
                                printf("    AssignedResources: %lu bytes\n", regSize);
                                found = ParseResourceList(regData, regSize, barPhys, barSize);
                            }
                        }

                        /* Also try Device Parameters subkey */
                        if (!found) {
                            HKEY hDevParamsKey;
                            if (RegOpenKeyExW(hInstKey, L"Device Parameters", 0, KEY_READ, &hDevParamsKey) == ERROR_SUCCESS) {
                                regSize = sizeof(regData);
                                if (RegQueryValueExW(hDevParamsKey, L"BootConfig", NULL, &regType, regData, &regSize) == ERROR_SUCCESS
                                    && regSize > 24) {
                                    printf("    Device Parameters\\BootConfig: %lu bytes\n", regSize);
                                    found = ParseResourceList(regData, regSize, barPhys, barSize);
                                }
                                if (!found) {
                                    regSize = sizeof(regData);
                                    if (RegQueryValueExW(hDevParamsKey, L"AssignedResources", NULL, &regType, regData, &regSize) == ERROR_SUCCESS
                                        && regSize > 24) {
                                        printf("    Device Parameters\\AssignedResources: %lu bytes\n", regSize);
                                        found = ParseResourceList(regData, regSize, barPhys, barSize);
                                    }
                                }
                                RegCloseKey(hDevParamsKey);
                            }
                        }

                        RegCloseKey(hInstKey);
                    }
                }

                if (found) break;
            }

            RegCloseKey(hDevKey);
        }

        if (found) break;
    }

    RegCloseKey(hPciKey);

    if (found) {
        printf("  >> BAR0: PA=0x%llX, Size=0x%X (%lu KB)\n",
            (unsigned long long)*barPhys, *barSize, *barSize / 1024);
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
    printf("[TEST 1/7] Find PCI BAR0 via registry enum...\n");
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
            printf("  reg[0x000C] = 0x%08X (scratch)\n", reg.Value);

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
            printf("  Fence:      %llu (expected >= 100)\n", (unsigned long long)hs.FenceValue);
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

        reg.Value = orig ^ 0x12345678;
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

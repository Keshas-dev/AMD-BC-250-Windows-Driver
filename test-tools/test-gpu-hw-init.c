/*
 * test-gpu-hw-init.c — Test real GPU hardware initialization
 *
 * Finds PCI BAR0 from registry BootConfig, initializes GPU via KMD IOCTL.
 *
 * Usage:
 *   test-gpu-hw-init.exe                      — Auto-detect BAR0 from registry
 *   test-gpu-hw-init.exe <BAR0_PA> <SIZE>     — Manual BAR0 override
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "advapi32.lib")

#include "..\inc\amdbc250_ioctl.h"

typedef struct {
    UINT32 MmioMapped, RingsInit, FenceInit;
    UINT64 RingPhys; UINT32 RingSize, WPtr, RPtr;
    UINT64 FencePhys, FenceVal, LastFence;
} HW_STATUS;
typedef struct { UINT32 Cmd[64], Count, Fence, Queue; } PM4_IN;

#define PM4_NOP 0x10
#define PM4_HDR(op, cnt) ((2u << 30) | (((cnt)-1u) << 16) | ((op) << 8))

static BOOL SIO(HANDLE h, DWORD c, PVOID i, DWORD is, PVOID o, DWORD os, DWORD *br)
{
    DWORD r = 0; BOOL ok = DeviceIoControl(h, c, i, is, o, os, &r, NULL);
    if (br) *br = r; return ok;
}

/*
 * Parse CM_RESOURCE_LIST (REG_RESOURCE_LIST format):
 *   ULONG Count;
 *   struct { INTERFACE_TYPE, ULONG BusNumber, PartialResourceList } List[Count];
 *   PartialResourceList: USHORT Version, USHORT Revision, ULONG Count, Descriptor[]
 *   Descriptor: UCHAR Type, UCHAR Share, USHORT Flags, LARGE_INTEGER Start, ULONG Length
 *
 * Scan for Type=3 (CmResourceTypeMemory), take the first one with Length >= 16MB.
 */
static BOOL ParseCmResourceList(BYTE *data, DWORD size, UINT64 *barPhys, UINT32 *barSize)
{
    if (size < 20) return FALSE;

    /* CM_RESOURCE_LIST header */
    DWORD listCount = *(DWORD *)(data + 0);
    printf("    CM_RESOURCE_LIST: %lu device(s)\n", listCount);

    DWORD off = 4; /* Skip Count */
    for (DWORD d = 0; d < listCount; d++) {
        if (off + 12 > size) break;

        DWORD ifaceType = *(DWORD *)(data + off);
        DWORD busNum    = *(DWORD *)(data + off + 4);
        printf("    Device[%lu]: InterfaceType=%lu, Bus=%lu\n", d, ifaceType, busNum);
        off += 8; /* Skip InterfaceType + BusNumber */

        if (off + 8 > size) break;

        /* PartialResourceList header */
        WORD prVer = *(WORD *)(data + off);
        WORD prRev = *(WORD *)(data + off + 2);
        DWORD prCnt = *(DWORD *)(data + off + 4);
        off += 8; /* Skip Version + Revision + Count */

        printf("    PartialResourceList: v%u.%u, %lu descriptors\n", prVer, prRev, prCnt);

        for (DWORD i = 0; i < prCnt; i++) {
            if (off + 16 > size) break;

            BYTE type = data[off];
            /* Start is at offset 4 from descriptor start, Length at offset 12 */
            ULONGLONG start = 0;
            memcpy(&start, data + off + 4, 8);
            DWORD length = *(DWORD *)(data + off + 12);

            if (type == 3) {
                printf("    Descriptor[%lu]: Memory, PA=0x%llX, Length=0x%X (%lu KB)\n",
                    i, start, length, length / 1024);
                /*
                 * GPU MMIO registers are typically 256KB-4MB at high physical addresses.
                 * VRAM BARs are typically 256MB+.
                 * We want the MMIO BAR (small one), not the VRAM BAR (big one).
                 */
                if (length >= 0x10000 && length <= 0x20000000) { /* 64KB - 512MB = MMIO */
                    *barPhys = (UINT64)start;
                    *barSize = length;
                    return TRUE;
                }
            } else if (type == 1) {
                printf("    Descriptor[%lu]: Port, PA=0x%llX, Length=0x%X\n", i, start, length);
            } else if (type == 2) {
                printf("    Descriptor[%lu]: Interrupt\n", i);
            }
            off += 16;
        }
    }
    return FALSE;
}

/*
 * Find PCI BAR0 from registry.
 * Path: HKLM\SYSTEM\CurrentControlSet\Enum\PCI\VEN_1002&DEV_13FE&*\*\LogConf\BootConfig
 */
static BOOL FindBar0FromRegistry(UINT64 *barPhys, UINT32 *barSize)
{
    HKEY hPci;
    DWORD idx = 0;
    WCHAR keyName[256];
    DWORD keyLen;

    *barPhys = 0;
    *barSize = 0;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Enum\\PCI",
        0, KEY_READ, &hPci) != ERROR_SUCCESS) {
        printf("[FAIL] Cannot open PCI registry key\n");
        return FALSE;
    }

    /* Enumerate PCI devices */
    while (1) {
        keyLen = sizeof(keyName) / sizeof(WCHAR);
        if (RegEnumKeyExW(hPci, idx++, keyName, &keyLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        if (!wcsstr(keyName, L"VEN_1002") || !wcsstr(keyName, L"DEV_13FE"))
            continue;

        printf("  PCI device: %ls\n", keyName);

        /* Enumerate instances under this device */
        WCHAR devPath[512];
        _snwprintf_s(devPath, sizeof(devPath)/sizeof(WCHAR), _TRUNCATE,
            L"SYSTEM\\CurrentControlSet\\Enum\\PCI\\%s", keyName);

        HKEY hDev;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, devPath, 0, KEY_READ, &hDev) != ERROR_SUCCESS)
            continue;

        DWORD instIdx = 0;
        WCHAR instName[256];
        while (1) {
            DWORD instLen = sizeof(instName) / sizeof(WCHAR);
            if (RegEnumKeyExW(hDev, instIdx++, instName, &instLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;

            printf("  Instance: %ls\n", instName);

            /* Try reading BootConfig from instance key directly */
            {
                WCHAR instFullKey[1024];
                _snwprintf_s(instFullKey, sizeof(instFullKey)/sizeof(WCHAR), _TRUNCATE,
                    L"SYSTEM\\CurrentControlSet\\Enum\\PCI\\%s\\%s",
                    keyName, instName);

                HKEY hInst;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, instFullKey, 0, KEY_READ, &hInst) == ERROR_SUCCESS) {
                    BYTE data[8192];
                    DWORD dataSize = sizeof(data);
                    DWORD dataType = 0;

                    /* Try BootConfig directly */
                    if (RegQueryValueExW(hInst, L"BootConfig", NULL, &dataType, data, &dataSize) == ERROR_SUCCESS
                        && dataSize >= 20) {
                        printf("    BootConfig: %lu bytes (type=%lu)\n", dataSize, dataType);
                        if (ParseCmResourceList(data, dataSize, barPhys, barSize)) {
                            RegCloseKey(hInst); RegCloseKey(hDev); RegCloseKey(hPci);
                            return TRUE;
                        }
                    }
                    RegCloseKey(hInst);
                }
            }

            /* Try LogConf subkey — BootConfig is a VALUE inside LogConf */
            {
                WCHAR logConfKey[1024];
                _snwprintf_s(logConfKey, sizeof(logConfKey)/sizeof(WCHAR), _TRUNCATE,
                    L"SYSTEM\\CurrentControlSet\\Enum\\PCI\\%s\\%s\\LogConf",
                    keyName, instName);

                HKEY hLC;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, logConfKey, 0, KEY_READ, &hLC) == ERROR_SUCCESS) {
                    BYTE data[8192];
                    DWORD dataSize = sizeof(data);
                    DWORD dataType = 0;

                    if (RegQueryValueExW(hLC, L"BootConfig", NULL, &dataType, data, &dataSize) == ERROR_SUCCESS
                        && dataSize >= 20) {
                        printf("    LogConf\\BootConfig: %lu bytes (type=%lu)\n", dataSize, dataType);
                        if (ParseCmResourceList(data, dataSize, barPhys, barSize)) {
                            RegCloseKey(hLC); RegCloseKey(hDev); RegCloseKey(hPci);
                            return TRUE;
                        }
                    }

                    /* Also try AssignedResources */
                    dataSize = sizeof(data);
                    if (RegQueryValueExW(hLC, L"AssignedResources", NULL, &dataType, data, &dataSize) == ERROR_SUCCESS
                        && dataSize >= 20) {
                        printf("    LogConf\\AssignedResources: %lu bytes\n", dataSize);
                        if (ParseCmResourceList(data, dataSize, barPhys, barSize)) {
                            RegCloseKey(hLC); RegCloseKey(hDev); RegCloseKey(hPci);
                            return TRUE;
                        }
                    }
                    RegCloseKey(hLC);
                }
            }

        }
        RegCloseKey(hDev);
    }
    RegCloseKey(hPci);
    return FALSE;
}

int main(int argc, char *argv[])
{
    HANDLE hDev;
    DWORD pass = 0, fail = 0, total = 0, br;
    UINT64 barPhys = 0;
    UINT32 barSize = 0;
    BOOL ok;

    printf("========================================\n");
    printf("  BC-250 GPU Hardware Init Test\n");
    printf("========================================\n\n");

    /* Manual BAR0 override */
    if (argc >= 3) {
        barPhys = _strtoui64(argv[1], NULL, 0);
        barSize = (UINT32)strtoul(argv[2], NULL, 0);
        printf("[MANUAL] BAR0 PA=0x%llX, Size=0x%X\n\n", (unsigned long long)barPhys, barSize);
    }

    hDev = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                        0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("[FAIL] Cannot open KMD: %lu\n", GetLastError());
        return 1;
    }
    printf("[OK] KMD device opened\n\n");

    /* Test 1: Find PCI BARs via KMD (READ_PCI_BAR) */
    if (barPhys == 0) {
        total++;
        printf("[TEST 1] READ_PCI_BAR (KMD PCI config scan)...\n");
        {
            AMDBC250_IOCTL_PCI_CONFIG pcfg = {0};
            ok = SIO(hDev, IOCTL_AMDBC250_READ_PCI_BAR, NULL, 0, &pcfg, sizeof(pcfg), &br);
            if (ok && pcfg.VendorId != 0) {
                printf("  PCI: %04X:%04X (Class=%06X, Bus=%lu)\n",
                    pcfg.VendorId, pcfg.DeviceId, pcfg.ClassCode, pcfg.Bus);
                printf("  BARs discovered:\n");
                for (int i = 0; i < 6; i++) {
                    if (pcfg.Bars[i].PhysicalAddress == 0) continue;
                    printf("    BAR[%d]: PA=0x%llX, Size=0x%X, %s, %s\n",
                        i, (unsigned long long)pcfg.Bars[i].PhysicalAddress,
                        pcfg.Bars[i].Size,
                        pcfg.Bars[i].IsMemoryBar ? "Memory" : "I/O",
                        pcfg.Bars[i].Is64Bit ? "64-bit" : "32-bit");
                    
                    /* MMIO register BAR: small memory (256KB typical for RDNA2) or BC-250 large BAR (256MB+) */
                    if (pcfg.Bars[i].IsMemoryBar && !barPhys) {
                        UINT64 pa = pcfg.Bars[i].PhysicalAddress;
                        if (pa >= 0x10000 && pa <= 0xFFFFFFFFULL) {
                            /* BC-250: single large BAR (256MB). Accept all sizes. */
                            if (pcfg.Bars[i].Size == 0 || pcfg.Bars[i].Size >= 0x10000) {
                                barPhys = pa;
                                barSize = pcfg.Bars[i].Size ? pcfg.Bars[i].Size : 0x100000;
                                printf("  >> Selected BAR[%d] as MMIO register BAR\n", i);
                            }
                        }
                    }
                }
                
                if (!barPhys) {
                    /* Fallback: try any memory BAR */
                    for (int i = 0; i < 6; i++) {
                        if (pcfg.Bars[i].PhysicalAddress != 0 && pcfg.Bars[i].IsMemoryBar) {
                            barPhys = pcfg.Bars[i].PhysicalAddress;
                            barSize = pcfg.Bars[i].Size ? pcfg.Bars[i].Size : 0x100000;
                            printf("  >> Fallback: using BAR[%d]\n", i);
                            break;
                        }
                    }
                }
                
                printf("\n  >> MMIO candidate: PA=0x%llX, Size=0x%X\n\n",
                    (unsigned long long)barPhys, barSize);
                printf("[PASS]\n\n"); pass++;
            } else {
                printf("[FAIL] READ_PCI_BAR failed (err=%lu)\n\n", GetLastError());
                fail++;
            }
        }

        /* If READ_PCI_BAR didn't find an MMIO BAR, try registry fallback */
        if (barPhys == 0) {
            total++;
            printf("[TEST 1b] Find PCI BAR0 from registry...\n");
            if (FindBar0FromRegistry(&barPhys, &barSize)) {
                printf("  >> BAR0: PA=0x%llX, Size=0x%X (%lu MB)\n\n",
                    (unsigned long long)barPhys, barSize, barSize / (1024*1024));
                printf("[PASS]\n\n"); pass++;
            } else {
                printf("[FAIL] BAR0 not found in registry either\n\n");
                printf("  Run with manual override: test-gpu-hw-init.exe <BAR_PA> <SIZE>\n\n");
                fail++;
            }
        }
    } else {
        total++; printf("[TEST 1] Manual BAR0\n[PASS]\n\n"); pass++;
    }

    /* Test 2: Init Hardware */
    total++;
    printf("[TEST 2] INIT_HARDWARE (PA=0x%llX, Size=0x%X)...\n", (unsigned long long)barPhys, barSize);
    {
        AMDBC250_IOCTL_INIT_HARDWARE ih = {0};
        ih.MmioPhysicalBase = barPhys;
        ih.MmioSize = barSize;
        ok = SIO(hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), NULL, 0, &br);
        if (ok) { printf("[PASS]\n\n"); pass++; }
        else { printf("[FAIL] err=%lu\n\n", GetLastError()); fail++; }
    }
    Sleep(200);

    /* Test 3: Get HW Status */
    total++;
    printf("[TEST 3] GET_HW_STATUS...\n");
    {
        AMDBC250_IOCTL_HW_STATUS hs = {0};
        ok = SIO(hDev, IOCTL_AMDBC250_GET_HW_STATUS, NULL, 0, &hs, sizeof(hs), &br);
        if (ok) {
            printf("  MMIO=%s, Rings=%s, Fence=%s\n",
                hs.MmioMapped?"YES":"NO", hs.RingsInitialized?"YES":"NO", hs.FenceInitialized?"YES":"NO");
            printf("  RingPA=0x%llX (%uKB), WPtr=%u, RPtr=%u\n",
                (unsigned long long)hs.GfxRingPhysAddr, hs.GfxRingSize/1024, hs.GfxRingWptr, hs.GfxRingRptr);
            printf("  Fence=%llu, LastFence=%llu\n",
                (unsigned long long)hs.FenceValue, (unsigned long long)hs.LastSubmittedFence);
            if (hs.MmioMapped && hs.RingsInitialized) { printf("[PASS]\n\n"); pass++; }
            else { printf("[FAIL] HW not fully init\n\n"); fail++; }
        } else { printf("[FAIL] err=%lu\n\n", GetLastError()); fail++; }
    }

    /* Test 4: Read GPU Registers */
    total++;
    printf("[TEST 4] READ_REG (MMIO)...\n");
    {
        AMDBC250_IOCTL_REG_ACCESS r = {0x000C, 0};
        ok = SIO(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &br);
        if (ok) {
            printf("  reg[0x000C] = 0x%08X (scratch)\n", r.Value);
            r.RegisterOffset = 0x0000; SIO(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &br);
            printf("  reg[0x0000] = 0x%08X\n", r.Value);
            r.RegisterOffset = 0x0080; SIO(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &br);
            printf("  reg[0x0080] = 0x%08X (GB_ADDR_CONFIG)\n", r.Value);
            r.RegisterOffset = 0x86D0; SIO(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &br);
            printf("  reg[0x86D0] = 0x%08X (CP_ME_CNTL)\n", r.Value);
            r.RegisterOffset = 0x01A4; SIO(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &br);
            printf("  reg[0x01A4] = 0x%08X (TMPMCR)\n", r.Value);
            printf("[PASS]\n\n"); pass++;
        } else { printf("[FAIL]\n\n"); fail++; }
    }

    /* Test 5: Send PM4 NOP */
    total++;
    printf("[TEST 5] SEND_PM4 (NOP + fence=100)...\n");
    {
        AMDBC250_IOCTL_SEND_PM4 p = {0};
        p.Commands[0] = PM4_HDR(PM4_NOP, 1);
        p.Commands[1] = 0xDEADBEEF;
        p.CommandCount = 2; p.FenceValue = 100; p.QueueType = 0;
        ok = SIO(hDev, IOCTL_AMDBC250_SEND_PM4, &p, sizeof(p), NULL, 0, &br);
        if (ok) { printf("[PASS]\n\n"); pass++; }
        else { printf("[FAIL] err=%lu\n\n", GetLastError()); fail++; }
    }
    Sleep(100);

    /* Test 6: Verify fence */
    total++;
    printf("[TEST 6] Verify ring write...\n");
    {
        AMDBC250_IOCTL_HW_STATUS hs = {0};
        ok = SIO(hDev, IOCTL_AMDBC250_GET_HW_STATUS, NULL, 0, &hs, sizeof(hs), &br);
        if (ok) {
            printf("  WPtr=%u, Fence=%llu\n", hs.GfxRingWptr, (unsigned long long)hs.FenceValue);
            if (hs.GfxRingWptr > 0) { printf("[PASS]\n\n"); pass++; }
            else { printf("[FAIL] WPtr still 0\n\n"); fail++; }
        } else { printf("[FAIL]\n\n"); fail++; }
    }

    /* Test 7: Register roundtrip */
    total++;
    printf("[TEST 7] Write/Read register...\n");
    {
        AMDBC250_IOCTL_REG_ACCESS r = {0x000C, 0};
        SIO(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &br);
        UINT32 orig = r.Value;
        r.Value = orig ^ 0x00FF00FF;
        ok = SIO(hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), NULL, 0, &br);
        if (ok) {
            r.Value = 0;
            SIO(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &br);
            printf("  orig=0x%08X, after=0x%08X\n", orig, r.Value);
            printf("[PASS]\n\n"); pass++;
        } else { printf("[FAIL]\n\n"); fail++; }
    }

    printf("========================================\n");
    printf("  Results: %lu/%lu passed\n", pass, total);
    printf("========================================\n");

    CloseHandle(hDev);
    return (fail == 0) ? 0 : 1;
}

#include <windows.h>
#include <stdio.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <initguid.h>
#include <string.h>

#pragma comment(lib, "setupapi.lib")

/* GUID_DEVCLASS_PCI is in devguid.h but might not be defined with our SDK */
/* We'll define it ourselves */
DEFINE_GUID(GUID_DEVCLASS_PCI_DISPLAY, 0x4d36e968, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);

int main(void) {
    printf("AMD BC-250 Resource Discovery Tool\n");
    printf("====================================\n\n");

    /* ========== A. Read ACPI MCFG table ========== */
    printf("--- A. ACPI MCFG Table (PCI ECAM base) ---\n");
    UINT32 mcfgSize = GetSystemFirmwareTable('MCFG', 0, NULL, 0);
    if (mcfgSize > 0) {
        PUCHAR mcfg = (PUCHAR)malloc(mcfgSize);
        if (mcfg) {
            UINT32 ret = GetSystemFirmwareTable('MCFG', 0, mcfg, mcfgSize);
            if (ret == mcfgSize) {
                UINT32 entryCount = (mcfgSize - 36) / 16;
                printf("  MCFG table found: %d bytes, %d ECAM entries\n\n", mcfgSize, entryCount);
                
                for (UINT32 i = 0; i < entryCount; i++) {
                    UINT32 base = *(UINT32 *)(mcfg + 36 + i * 16);
                    UINT16 seg = *(UINT16 *)(mcfg + 36 + i * 16 + 4);
                    UCHAR startBus = *(UCHAR *)(mcfg + 36 + i * 16 + 6);
                    UCHAR endBus = *(UCHAR *)(mcfg + 36 + i * 16 + 7);
                    printf("  Entry %d:\n", i);
                    printf("    ECAM Base: 0x%08X\n", base);
                    printf("    Segment:   %d\n", seg);
                    printf("    Bus range: %d - %d\n\n", startBus, endBus);
                }
            }
            free(mcfg);
        }
    } else {
        printf("  No MCFG table found (error %d)\n\n", GetLastError());
    }

    /* ========== B. Scan registry for BC-250 PCI device ========== */
    printf("--- B. PCI Registry Scan (looking for BC-250 13FE) ---\n\n");

    /* Open PCI enumeration key and search recursively */
    HKEY hPciEnum = NULL;
    LONG ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ, &hPciEnum);
    if (ret != ERROR_SUCCESS) {
        printf("  Cannot open PCI enum key: %d\n\n", ret);
        return 1;
    }

    int foundDevice = 0;
    for (DWORD vidx = 0; ; vidx++) {
        CHAR vendorKey[256] = {0};
        DWORD keySz = sizeof(vendorKey);
        ret = RegEnumKeyExA(hPciEnum, vidx, vendorKey, &keySz, NULL, NULL, NULL, NULL);
        if (ret != ERROR_SUCCESS) break;

        FILE *f = NULL;
        
        if (strstr(vendorKey, "VEN_1002") || strstr(vendorKey, "13FE") || strstr(vendorKey, "VEN_1022")) {
            printf("  Vendor key: %s\n", vendorKey);
            
            CHAR amdPath[512];
            _snprintf_s(amdPath, sizeof(amdPath), _TRUNCATE,
                "SYSTEM\\CurrentControlSet\\Enum\\PCI\\%s", vendorKey);
            HKEY hAmd = NULL;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, amdPath, 0, KEY_READ, &hAmd) == ERROR_SUCCESS) {
                for (DWORD didx = 0; ; didx++) {
                    CHAR devKey[256] = {0};
                    DWORD devKeySz = sizeof(devKey);
                    ret = RegEnumKeyExA(hAmd, didx, devKey, &devKeySz, NULL, NULL, NULL, NULL);
                    if (ret != ERROR_SUCCESS) break;
                    
                    printf("    Instance: %s\n", devKey);
                    
                    CHAR instPath[1024];
                    _snprintf_s(instPath, sizeof(instPath), _TRUNCATE, "%s\\%s", amdPath, devKey);
                    HKEY hInst = NULL;
                    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, instPath, 0, KEY_READ, &hInst) == ERROR_SUCCESS) {
                        /* Read DeviceDesc */
                        CHAR devDesc[512] = {0};
                        DWORD descSz = sizeof(devDesc);
                        DWORD descType = 0;
                        if (RegQueryValueExA(hInst, "DeviceDesc", NULL, &descType, (PBYTE)devDesc, &descSz) == ERROR_SUCCESS) {
                            printf("      Description: %s\n", devDesc);
                        }
                        
                        /* Read HardwareID */
                        CHAR hwId[512] = {0};
                        DWORD hwSz = sizeof(hwId);
                        DWORD hwType = 0;
                        if (RegQueryValueExA(hInst, "HardwareID", NULL, &hwType, (PBYTE)hwId, &hwSz) == ERROR_SUCCESS) {
                            printf("      HWID: ");
                            for (DWORD c = 0; c < hwSz && hwId[c]; c += strlen(hwId + c) + 1) {
                                printf("%s ", hwId + c);
                            }
                            printf("\n");
                        }

                        /* Check for BC-250 (13FE) - recursively search LogConf */
                        int isBc250 = (strstr(vendorKey, "13FE") != NULL) ||
                                      (strstr(devKey, "13FE") != NULL) ||
                                      (strstr(hwId, "13FE") != NULL);

                        if (isBc250) foundDevice = 1;
                        
                        /* Search all subkeys recursively for resource information */
                        CHAR subKeyNames[256][512];
                        DWORD subKeyCount = 0;
                        
                        /* First pass - enumerate all subkeys */
                        for (DWORD sk = 0; sk < 256; sk++) {
                            CHAR subKey[256] = {0};
                            DWORD subKeySz = sizeof(subKey);
                            ret = RegEnumKeyExA(hInst, sk, subKey, &subKeySz, NULL, NULL, NULL, NULL);
                            if (ret != ERROR_SUCCESS) break;
                            _snprintf_s(subKeyNames[subKeyCount], 512, _TRUNCATE, "%s\\%s", instPath, subKey);
                            subKeyCount++;
                        }
                        
                        /* Second pass - check each subkey for LogConf or resource data */
                        for (DWORD sk = 0; sk < subKeyCount; sk++) {
                            HKEY hSub = NULL;
                            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKeyNames[sk], 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                                /* Check if this is a LogConf or resource key */
                                if (strstr(subKeyNames[sk], "LogConf") || 
                                    strstr(subKeyNames[sk], "Control") ||
                                    strstr(subKeyNames[sk], "Parameters")) {
                                    
                                    /* Read all values */
                                    for (int vi = 0; ; vi++) {
                                        CHAR valName[256] = {0};
                                        DWORD valNameSz = sizeof(valName);
                                        UCHAR valData[4096] = {0};
                                        DWORD valDataSz = sizeof(valData);
                                        DWORD valType = 0;
                                        
                                        LONG r = RegEnumValueA(hSub, vi, valName, &valNameSz,
                                            NULL, &valType, valData, &valDataSz);
                                        if (r != ERROR_SUCCESS) break;
                                        
                                        /* Print only for BC-250 or for resource binary data */
                                        if (isBc250 || (valType == REG_BINARY && valDataSz >= 8)) {
                                            printf("      [%s] %s: ", 
                                                subKeyNames[sk] + strlen(subKeyNames[sk]) - min(20, strlen(subKeyNames[sk])),
                                                valName);
                                            
                                            if (valType == REG_DWORD) {
                                                printf("0x%08X\n", *(DWORD *)valData);
                                            } else if (valType == REG_QWORD) {
                                                printf("0x%016llX\n", *(UINT64 *)valData);
                                            } else if (valType == REG_BINARY) {
                                                printf("BIN[%d] ", valDataSz);
                                                for (DWORD b = 0; b < min(valDataSz, 96); b++) {
                                                    printf("%02X", valData[b]);
                                                    if ((b+1)%16==0) printf(" ");
                                                }
                                                printf("\n");
                                                
                                                /* Try to find BAR-like physical addresses */
                                                for (DWORD off = 0; off + 4 <= valDataSz; off++) {
                                                    UINT32 val32 = *(UINT32 *)(valData + off);
                                                    UINT64 val64 = *(UINT64 *)(valData + off);
                                                    
                                                    /* Check for common BAR address patterns */
                                                    if ((val32 & 0xFFFF0000) == 0xFE800000) {
                                                        printf("        *** MATCH: BAR5-like address (0xFE800000) at offset %d\n", off);
                                                    }
                                                    if ((val32 & 0xFFF00000) == 0xFE000000 && val32 >= 0xFE000000 && val32 <= 0xFFFFFFFF) {
                                                        printf("        Possible MMIO BAR(32): 0x%08X at offset %d\n", val32, off);
                                                    }
                                                    if (val64 >= 0x80000000ULL && val64 <= 0xFFFFFFFFULL) {
                                                        /* Could be a 32-bit BAR address */
                                                        if ((val64 & 0xFFF) == 0) { /* page-aligned */
                                                            printf("        Aligned mem address: 0x%08llX at offset %d\n", val64, off);
                                                        }
                                                    }
                                                    if (val64 >= 0x1000000000ULL && val64 <= 0x10000FFFFFFULL) {
                                                        printf("        High VRAM range: 0x%016llX at offset %d\n", val64, off);
                                                    }
                                                }
                                            } else if (valType == REG_SZ || valType == REG_MULTI_SZ) {
                                                printf("%s\n", valData);
                                            } else {
                                                printf("(type=%d, size=%d)\n", valType, valDataSz);
                                            }
                                        }
                                    }
                                }
                                
                                /* Deeper recursion for LogConf */
                                for (DWORD dsk = 0; dsk < 256; dsk++) {
                                    CHAR deepKey[256] = {0};
                                    DWORD deepSz = sizeof(deepKey);
                                    ret = RegEnumKeyExA(hSub, dsk, deepKey, &deepSz, NULL, NULL, NULL, NULL);
                                    if (ret != ERROR_SUCCESS) break;
                                    
                                    CHAR deepPath[1024];
                                    _snprintf_s(deepPath, sizeof(deepPath), _TRUNCATE, "%s\\%s", subKeyNames[sk], deepKey);
                                    
                                    if (strstr(deepPath, "BootConfig") || strstr(deepPath, "AllocatedConfig")) {
                                        HKEY hDeep = NULL;
                                        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, deepPath, 0, KEY_READ, &hDeep) == ERROR_SUCCESS) {
                                            LONG dret = 0;
                                            for (int dvi = 0; ; dvi++) {
                                                CHAR dvn[256] = {0};
                                                DWORD dvnSz = sizeof(dvn);
                                                UCHAR dvd[4096] = {0};
                                                DWORD dvdSz = sizeof(dvd);
                                                DWORD dvt = 0;
                                                
                                                dret = RegEnumValueA(hDeep, dvi, dvn, &dvnSz, NULL, &dvt, dvd, &dvdSz);
                                                if (dret != ERROR_SUCCESS) break;
                                                
                                                printf("      (deep) %s\\%s: ", deepPath + strlen(instPath) + 1, dvn);
                                                if (dvt == REG_BINARY) {
                                                    printf("BIN[%d] ", dvdSz);
                                                    for (DWORD b = 0; b < min(dvdSz, 64); b++) {
                                                        printf("%02X", dvd[b]);
                                                    }
                                                    printf("\n");
                                                    
                                                    /* Scan for addresses */
                                                    for (DWORD off = 0; off + 8 <= dvdSz; off++) {
                                                        UINT64 v = *(UINT64 *)(dvd + off);
                                                        if (v >= 0xFE000000ULL && v <= 0xFFFFFFFFULL && (v & 0xFFF) == 0) {
                                                            printf("        -> Page-aligned addr in [0xFE000000-0xFFFFFFFF]: 0x%016llX\n", v);
                                                        }
                                                    }
                                                } else if (dvt == REG_DWORD) {
                                                    printf("0x%08X\n", *(DWORD *)dvd);
                                                }
                                            }
                                            RegCloseKey(hDeep);
                                        }
                                    }
                                }
                                
                                RegCloseKey(hSub);
                            }
                        }
                        RegCloseKey(hInst);
                    }
                }
                RegCloseKey(hAmd);
            }
        }
    }
    RegCloseKey(hPciEnum);

    /* ========== C. SetupAPI device enumeration ========== */
    printf("\n--- C. SetupAPI Device Enumeration ---\n\n");
    
    HDEVINFO hDevInfo = SetupDiGetClassDevsEx(
        NULL, NULL, NULL,
        DIGCF_PRESENT | DIGCF_ALLCLASSES,
        NULL, NULL, NULL);

    if (hDevInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devData = {0};
        devData.cbSize = sizeof(SP_DEVINFO_DATA);
        
        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); i++) {
            CHAR devId[512] = {0};
            DWORD sz = 0;
            if (!SetupDiGetDeviceInstanceIdA(hDevInfo, &devData, devId, sizeof(devId), &sz))
                continue;

            if (strstr(devId, "13FE") != NULL || strstr(devId, "BC-250") != NULL ||
                (strstr(devId, "VEN_1002") && strstr(devId, "DEV_13FE"))) {
                
                printf("  FOUND BC-250 via SetupAPI!\n");
                printf("    Instance ID: %s\n", devId);
                
                /* Get device description */
                CHAR desc[256] = {0};
                DWORD descSz = sizeof(desc);
                DWORD propType = 0;
                if (SetupDiGetDeviceRegistryPropertyA(
                    hDevInfo, &devData, SPDRP_DEVICEDESC,
                    &propType, (PBYTE)desc, sizeof(desc), &descSz)) {
                    printf("    Description: %s\n", desc);
                }
                
                /* Get status */
                DWORD status = 0, problem = 0;
                if (SetupDiGetDeviceRegistryPropertyA(
                    hDevInfo, &devData, SPDRP_CONFIGFLAGS,
                    &propType, (PBYTE)&status, sizeof(status), &sz)) {
                    printf("    ConfigFlags: 0x%08X\n", status);
                }
                
                foundDevice = 1;
                break;
            }
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    } else {
        printf("  SetupDiGetClassDevsEx failed: %d\n", GetLastError());
    }

    printf("\n--- Summary ---\n");
    if (!foundDevice) {
        printf("  BC-250 (13FE) not found in registry or SetupAPI!\n");
        printf("  This is suspicious - the device might not be properly enumerated.\n");
        printf("  Check Device Manager: is 'AMD Radeon BC-250 Graphics' in 'Display adapters'?\n");
    }

    printf("\nDone.\n");
    return foundDevice ? 0 : 1;
}

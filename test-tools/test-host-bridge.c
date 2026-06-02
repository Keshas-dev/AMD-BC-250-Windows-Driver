#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *TypeStr(UINT8 t) {
    switch(t) {
        case 0: return "Null";
        case 1: return "Port";
        case 2: return "Interrupt";
        case 3: return "Memory";
        case 4: return "DMA";
        case 5: return "DeviceSpecific";
        case 6: return "BusNumber";
        case 7: return "MemoryLarge";
        case 0x81: return "IoRange";
        case 0x82: return "MemoryRange";
        default: return "?";
    }
}

static void DumpAndParse(const char *label, PUCHAR d, DWORD sz) {
    printf("\n=== %s (%d bytes) ===\n", label, sz);
    for (DWORD i = 0; i < sz; i += 16) {
        printf("  %04X: ", i);
        for (DWORD j = 0; j < 16 && (i+j) < sz; j++) printf("%02X ", d[i+j]);
        printf("\n");
    }
    /* Look for CM_PARTIAL_RESOURCE_DESCRIPTOR pattern: Type + Share + Flags + Reserved + union */
    printf("\n  Parsing resource descriptors (20 bytes each):\n");
    for (DWORD i = 0; i + 19 < sz; i += 20) {
        UCHAR type = d[i];
        UCHAR share = d[i+1];
        USHORT flags = d[i+2] | (d[i+3] << 8);
        ULONG reserved = d[i+4] | (d[i+5]<<8) | (d[i+6]<<16) | (d[i+7]<<24);
        UINT64 start = *(UINT64 *)(d + i + 8);
        ULONG length = *(UINT32 *)(d + i + 16);
        
        printf("  [%03X] Type=%d(%s) Share=%d Flags=0x%04X Start=0x%016llX Len=0x%X",
            i, type, TypeStr((UINT8)type), share, flags, start, length);
        if (type == 3 && start >= 0xE0000000ULL && start <= 0xFFFFFFFFULL) {
            printf(" <<< MMIO region?");
        }
        if (type == 3 && start >= 0x80000000ULL && start <= 0xFFFFFFFFULL && length >= 0x1000000) {
            printf(" ***");
        }
        printf("\n");
    }
    /* Also dump any addresses as 8-byte values */
    printf("\n  All aligned potential addresses:\n");
    for (DWORD off = 0; off + 8 <= sz; off += 8) {
        UINT64 val = *(UINT64 *)(d + off);
        if (val >= 0xE0000000ULL && val <= 0xFFFFFFFFULL && (val & 0xFFF) == 0) {
            printf("    +0x%03X: 0x%016llX\n", off, val);
        }
    }
}

int main(void) {
    printf("=== Host Bridge (0x13E0) BootConfig ===\n\n");
    const char *devPath = "SYSTEM\\CurrentControlSet\\Enum\\PCI\\VEN_1022&DEV_13E0&SUBSYS_14501022&REV_00\\3&11583659&0&00";
    
    HKEY hDev = NULL;
    LONG ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, devPath, 0, KEY_READ, &hDev);
    if (ret == ERROR_SUCCESS) {
        CHAR loc[256]={0}; DWORD sz=sizeof(loc); DWORD type=0;
        if (RegQueryValueExA(hDev,"LocationInformation",NULL,&type,(PBYTE)loc,&sz)==ERROR_SUCCESS)
            printf("  Location: %s\n", loc);
        RegCloseKey(hDev);
    }
    
    char logPath[1024];
    _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%s\\LogConf", devPath);
    HKEY hLog = NULL;
    ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, logPath, 0, KEY_READ, &hLog);
    if (ret == ERROR_SUCCESS) {
        for (int vi = 0; ; vi++) {
            CHAR valName[256]={0}; DWORD vns=sizeof(valName);
            UCHAR valData[8192]={0}; DWORD vds=sizeof(valData); DWORD vt=0;
            ret = RegEnumValueA(hLog, vi, valName, &vns, NULL, &vt, valData, &vds);
            if (ret != ERROR_SUCCESS) break;
            if (vt == REG_BINARY || vt == REG_RESOURCE_LIST || vt == REG_RESOURCE_REQUIREMENTS_LIST)
                DumpAndParse(valName, valData, vds);
        }
        RegCloseKey(hLog);
    } else printf("  No LogConf: %d\n", ret);
    
    /* Also check all other host bridge instances for resources */
    printf("\n=== Other Host Bridge instances ===\n");
    HKEY hVendor = NULL;
    ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum\\PCI\\VEN_1022&DEV_13E0&SUBSYS_14501022&REV_00", 0, KEY_READ, &hVendor);
    if (ret == ERROR_SUCCESS) {
        for (int vi = 0; ; vi++) {
            CHAR inst[256]={0}; DWORD isz=sizeof(inst);
            ret = RegEnumKeyExA(hVendor, vi, inst, &isz, NULL, NULL, NULL, NULL);
            if (ret != ERROR_SUCCESS) break;
            printf("  Instance: %s\n", inst);
        }
        RegCloseKey(hVendor);
    }
    
    /* Also check VEN_1022&DEV_13E2 (another host bridge) */
    printf("\n=== DEV_13E2 (PCI standard host CPU bridge) ===\n");
    HKEY hE2 = NULL;
    ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum\\PCI\\VEN_1022&DEV_13E2&SUBSYS_00000000&REV_00", 0, KEY_READ, &hE2);
    if (ret == ERROR_SUCCESS) {
        for (int vi = 0; ; vi++) {
            CHAR inst[256]={0}; DWORD isz=sizeof(inst);
            ret = RegEnumKeyExA(hE2, vi, inst, &isz, NULL, NULL, NULL, NULL);
            if (ret != ERROR_SUCCESS) break;
            
            CHAR instPath[1024];
            _snprintf_s(instPath, sizeof(instPath), _TRUNCATE, 
                "SYSTEM\\CurrentControlSet\\Enum\\PCI\\VEN_1022&DEV_13E2&SUBSYS_00000000&REV_00\\%s\\LogConf", inst);
            HKEY hIL = NULL;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, instPath, 0, KEY_READ, &hIL) == ERROR_SUCCESS) {
                printf("  Instance %s has LogConf:\n", inst);
                for (int vi2 = 0; ; vi2++) {
                    CHAR valName[256]={0}; DWORD vns=sizeof(valName);
                    UCHAR valData[8192]={0}; DWORD vds=sizeof(valData); DWORD vt=0;
                    ret = RegEnumValueA(hIL, vi2, valName, &vns, NULL, &vt, valData, &vds);
                    if (ret != ERROR_SUCCESS) break;
                    printf("    %s: %d bytes (type=%d)\n", valName, vds, vt);
                }
                RegCloseKey(hIL);
            }
        }
        RegCloseKey(hE2);
    }

    printf("\n=== DEV_13F0-13F7 (Data Fabric) ===\n");
    const char *dfIds[] = {"13F0","13F1","13F2","13F3","13F4","13F5","13F6","13F7"};
    for (int i = 0; i < 8; i++) {
        char keyPath[512];
        _snprintf_s(keyPath, sizeof(keyPath), _TRUNCATE,
            "SYSTEM\\CurrentControlSet\\Enum\\PCI\\VEN_1022&DEV_%s&SUBSYS_00000000&REV_00", dfIds[i]);
        HKEY hDF = NULL;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hDF) == ERROR_SUCCESS) {
            for (int vi = 0; ; vi++) {
                CHAR inst[256]={0}; DWORD isz=sizeof(inst);
                ret = RegEnumKeyExA(hDF, vi, inst, &isz, NULL, NULL, NULL, NULL);
                if (ret != ERROR_SUCCESS) break;
                
                char lcPath[1024];
                _snprintf_s(lcPath, sizeof(lcPath), _TRUNCATE, "%s\\%s\\LogConf", keyPath, inst);
                HKEY hLC = NULL;
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, lcPath, 0, KEY_READ, &hLC) == ERROR_SUCCESS) {
                    printf("  DF %s instance %s has LogConf\n", dfIds[i], inst);
                    
                    for (int vi2 = 0; ; vi2++) {
                        CHAR valName[256]={0}; DWORD vns=sizeof(valName);
                        UCHAR valData[8192]={0}; DWORD vds=sizeof(valData); DWORD vt=0;
                        ret = RegEnumValueA(hLC, vi2, valName, &vns, NULL, &vt, valData, &vds);
                        if (ret != ERROR_SUCCESS) break;
                        
                        if (vt == REG_BINARY || vt == REG_RESOURCE_LIST || vt == REG_RESOURCE_REQUIREMENTS_LIST) {
                            DumpAndParse(valName, valData, vds);
                        }
                    }
                    RegCloseKey(hLC);
                }
            }
            RegCloseKey(hDF);
        }
    }
    
    printf("\nDone.\n");
    return 0;
}

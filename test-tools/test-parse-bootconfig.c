#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Parse PCI resource descriptors from binary BootConfig data */
typedef struct {
    UINT64 Start;
    UINT64 Length;
    UINT8  Type;    /* 0=Memory, 1=IOPort, 2=Interrupt, etc. */
    UINT8  Flags;
} PARSE_RESOURCE;

static const char *ResourceTypeStr(UINT8 type) {
    static const char *names[] = {
        "Reserved", "Memory", "IOPort", "Interrupt", "DMA", "DMA", "DeviceSpecific", 
        "BusNumber", "Memory24", "MemoryWindow", "IORange"
    };
    if (type <= 10) return names[type];
    return "Unknown";
}

static void ParseLogConfBinary(PUCHAR data, DWORD size, const char *label) {
    printf("\n=== %s (%d bytes) ===\n", label, size);
    
    /* Hex dump */
    for (DWORD i = 0; i < size; i += 16) {
        printf("  %04X: ", i);
        for (DWORD j = 0; j < 16 && (i + j) < size; j++)
            printf("%02X ", data[i + j]);
        printf("  ");
        for (DWORD j = 0; j < 16 && (i + j) < size; j++) {
            UCHAR c = data[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }
    
    /* Try to parse as CM_PARTIAL_RESOURCE_LIST structure */
    printf("\n  Parsing as raw resource descriptors...\n");
    
    /* Look for aligned 64-bit values that could be memory addresses */
    printf("\n  Possible Memory Resources (64-bit aligned values):\n");
    int memFound = 0;
    for (DWORD off = 0; off + 8 <= size; off++) {
        UINT64 val = *(UINT64 *)(data + off);
        /* Check for typical MMIO BAR ranges */
        if (val >= 0xE0000000ULL && val <= 0xFFFFFFFFULL && (val & 0xFFF) == 0) {
            printf("    [0x%03X] Possible MMIO BAR: 0x%08llX (size aligned)\n", off, val);
            memFound = 1;
        }
        /* Check for high 64-bit addresses (VRAM) */
        if (val >= 0x1000000000ULL && val <= 0x2000000000ULL && (val & 0xFFFFF) == 0) {
            printf("    [0x%03X] Possible VRAM BAR: 0x%016llX (1MB aligned)\n", off, val);
            memFound = 1;
        }
        /* Check for small values that could be sizes */
        if (val >= 0x100000 && val <= 0x20000000 && (val & 0xFFF) == 0) {
            printf("    [0x%03X] Possible size value: 0x%llX (%llu KB/%llu MB)\n", 
                off, val, val/1024, val/(1024*1024));
        }
    }
    if (!memFound) printf("    (no memory addresses found)\n");
    
    /* Try interpreting as pairs (base, size) */
    printf("\n  Base+Size pairs (every 16 bytes):\n");
    for (DWORD off = 0; off + 16 <= size; off += 16) {
        UINT64 base = *(UINT64 *)(data + off);
        UINT64 len = *(UINT64 *)(data + off + 8);
        if (len > 0 && len < 0x20000000) {
            printf("    [%03X] Base=0x%016llX  Size=0x%llX (%llu %s)\n", 
                off, base, len, 
                len >= 0x100000 ? len / (1024*1024) : len / 1024,
                len >= 0x100000 ? "MB" : "KB");
        }
    }
    
    /* Try PARENT_RESOURCE / IO_RESOURCE / MEMORY_RESOURCE descriptors */
    printf("\n  Analysis based on Windows resource descriptor format:\n");
    printf("  (expecting CM_PARTIAL_RESOURCE_DESCRIPTOR = 4+4+4+8+8+4 = 32 bytes each or 16)\n");
    
    /* Search for known descriptor signatures */
    for (DWORD off = 0; off + 4 <= size; off++) {
        UINT32 tag = data[off] | (data[off+1] << 8) | (data[off+2] << 16) | (data[off+3] << 24);
        /* Look for CM_PARTIAL_RESOURCE_DESCRIPTOR patterns:
           Type=0x01 (Memory), ShareDisposition=0x01/0x03, Flags=0x00/0x0A */
        if (tag == 0x00000001 || tag == 0x03000001 || tag == 0x01000001) {
            UINT64 addr = *(UINT64 *)(data + off + 4);
            UINT32 len_low = *(UINT32 *)(data + off + 12);
            UINT64 len64 = *(UINT64 *)(data + off + 12);
            printf("\n    [%03X] CmMemory descriptor? Type=0x%08X", off, tag);
            printf("\n         Address=0x%08llX", addr);
            printf("  Length(32)=0x%X  Length(64)=0x%llX\n", len_low, len64);
        }
        /* IOPort: Type=0x02 */
        if (tag == 0x00000002 || tag == 0x03000002) {
            UINT64 addr = *(UINT64 *)(data + off + 4);
            printf("\n    [%03X] CmIOPort descriptor? Addr=0x%016llX\n", off, addr);
        }
    }
}

int main(void) {
    printf("AMD BC-250 BootConfig Parser\n");
    printf("=============================\n\n");

    const char *path = "SYSTEM\\CurrentControlSet\\Enum\\PCI\\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\\4&19caf403&0&0041\\LogConf";

    HKEY hLogConf = NULL;
    LONG ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hLogConf);
    if (ret != ERROR_SUCCESS) {
        printf("Cannot open LogConf key: %d\n", ret);
        return 1;
    }

    /* Enumerate values */
    for (int vi = 0; ; vi++) {
        CHAR valName[256] = {0};
        DWORD valNameSz = sizeof(valName);
        UCHAR valData[8192] = {0};
        DWORD valDataSz = sizeof(valData);
        DWORD valType = 0;
        
        ret = RegEnumValueA(hLogConf, vi, valName, &valNameSz, NULL, &valType, valData, &valDataSz);
        if (ret != ERROR_SUCCESS) break;
        
        if (valType == REG_BINARY || valType == REG_RESOURCE_LIST || valType == REG_RESOURCE_REQUIREMENTS_LIST) {
            ParseLogConfBinary(valData, valDataSz, valName);
        } else {
            printf("  %s: type=%d, size=%d (not binary)\n", valName, valType, valDataSz);
        }
    }

    RegCloseKey(hLogConf);
    
    /* Also try reading Control key for bus/device/function info */
    printf("\n\n=== Device Control Information ===\n");
    char controlPath[1024];
    _snprintf_s(controlPath, sizeof(controlPath), _TRUNCATE,
        "SYSTEM\\CurrentControlSet\\Enum\\PCI\\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\\4&19caf403&0&0041\\Control");
    
    HKEY hCtrl = NULL;
    ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, controlPath, 0, KEY_READ, &hCtrl);
    if (ret == ERROR_SUCCESS) {
        for (int vi = 0; ; vi++) {
            CHAR valName[256] = {0};
            DWORD valNameSz = sizeof(valName);
            UCHAR valData[4096] = {0};
            DWORD valDataSz = sizeof(valData);
            DWORD valType = 0;
            
            ret = RegEnumValueA(hCtrl, vi, valName, &valNameSz, NULL, &valType, valData, &valDataSz);
            if (ret != ERROR_SUCCESS) break;
            
            if (valType == REG_DWORD) {
                printf("  %s = 0x%08X (%d)\n", valName, *(DWORD *)valData, *(DWORD *)valData);
            } else if (valType == REG_BINARY) {
                printf("  %s: BIN[%d] ", valName, valDataSz);
                for (DWORD b = 0; b < min(valDataSz, 32); b++) printf("%02X", valData[b]);
                printf("\n");
            } else {
                printf("  %s: type=%d size=%d\n", valName, valType, valDataSz);
            }
        }
        RegCloseKey(hCtrl);
    } else {
        printf("  No Control key: %d\n", ret);
    }

    /* Read device config and hardware info */
    printf("\n=== Full Device Enumeration Key ===\n");
    char devPath[1024];
    _snprintf_s(devPath, sizeof(devPath), _TRUNCATE,
        "SYSTEM\\CurrentControlSet\\Enum\\PCI\\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00\\4&19caf403&0&0041");
    
    HKEY hDev = NULL;
    ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, devPath, 0, KEY_READ, &hDev);
    if (ret == ERROR_SUCCESS) {
        for (int vi = 0; ; vi++) {
            CHAR valName[256] = {0};
            DWORD valNameSz = sizeof(valName);
            UCHAR valData[4096] = {0};
            DWORD valDataSz = sizeof(valData);
            DWORD valType = 0;
            
            ret = RegEnumValueA(hDev, vi, valName, &valNameSz, NULL, &valType, valData, &valDataSz);
            if (ret != ERROR_SUCCESS) break;
            
            if (valType == REG_DWORD) {
                printf("  %s = 0x%08X (%d)\n", valName, *(DWORD *)valData, *(DWORD *)valData);
            } else if (valType == REG_BINARY) {
                printf("  %s: BIN[%d]", valName, valDataSz);
                if (valNameSz == 0 && valDataSz >= 8) {
                    printf(" -> Possible resource descriptor");
                    UINT64 *pq = (UINT64 *)valData;
                    for (DWORD q = 0; q < min(valDataSz/8, 4); q++)
                        printf(" [0x%016llX]", pq[q]);
                }
                printf("\n");
            } else if (valType == REG_SZ) {
                printf("  %s = %s\n", valName, valData);
            } else {
                printf("  %s: type=%d size=%d\n", valName, valType, valDataSz);
            }
        }
        RegCloseKey(hDev);
    }

    return 0;
}

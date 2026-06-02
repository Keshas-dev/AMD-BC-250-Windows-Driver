#include <windows.h>
#include <stdio.h>
#include <string.h>

static void DumpBinary(const char *label, PUCHAR data, DWORD size) {
    printf("\n=== %s (%d bytes) ===\n", label, size);
    for (DWORD i = 0; i < size; i += 16) {
        printf("  %04X: ", i);
        for (DWORD j = 0; j < 16 && (i + j) < size; j++)
            printf("%02X ", data[i + j]);
        printf("  ");
        for (DWORD j = 0; j < 16 && (i + j) < size; j++)
            printf("%c", (data[i + j] >= 32 && data[i + j] < 127) ? data[i + j] : '.');
        printf("\n");
    }
    /* Look for aligned memory addresses */
    printf("  Possible MMIO BARs:\n");
    for (DWORD off = 0; off + 8 <= size; off += 4) {
        UINT64 val64 = *(UINT64 *)(data + off);
        UINT32 val32 = *(UINT32 *)(data + off);
        if (val64 >= 0xE0000000ULL && val64 <= 0xFFFFFFFFULL && (val64 & 0xFFF) == 0)
            printf("    [0x%03X] 64-bit BAR candidate: 0x%08llX (%llu KB)\n", off, val64, 
                (off + 8 <= size) ? (val64 > 0x100000 ? val64/0x100000 : val64/0x400) : 0);
        if (val32 >= 0xE0000000 && val32 <= 0xFFFFFFFF && (val32 & 0xFFF) == 0)
            printf("    [0x%03X] 32-bit BAR candidate: 0x%08X\n", off, val32);
    }
}

int main(void) {
    printf("AMD PSP/CCP (DEV_143E) BootConfig Reader\n");
    printf("==========================================\n\n");

    const char *devPath = "SYSTEM\\CurrentControlSet\\Enum\\PCI\\VEN_1022&DEV_143E&SUBSYS_00001022&REV_00\\4&19caf403&0&0241";

    /* Read main device key */
    printf("--- Device Properties ---\n");
    HKEY hDev = NULL;
    LONG ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, devPath, 0, KEY_READ, &hDev);
    if (ret == ERROR_SUCCESS) {
        CHAR devDesc[256] = {0}; DWORD sz = sizeof(devDesc); DWORD type = 0;
        if (RegQueryValueExA(hDev, "DeviceDesc", NULL, &type, (PBYTE)devDesc, &sz) == ERROR_SUCCESS)
            printf("  DeviceDesc: %s\n", devDesc);
        
        CHAR loc[256] = {0}; sz = sizeof(loc);
        if (RegQueryValueExA(hDev, "LocationInformation", NULL, &type, (PBYTE)loc, &sz) == ERROR_SUCCESS)
            printf("  Location: %s\n", loc);
        
        DWORD cfg = 0; sz = sizeof(cfg);
        if (RegQueryValueExA(hDev, "ConfigFlags", NULL, &type, (PBYTE)&cfg, &sz) == ERROR_SUCCESS)
            printf("  ConfigFlags: 0x%08X (%d)\n", cfg, cfg);
        
        DWORD caps = 0; sz = sizeof(caps);
        if (RegQueryValueExA(hDev, "Capabilities", NULL, &type, (PBYTE)&caps, &sz) == ERROR_SUCCESS)
            printf("  Capabilities: 0x%08X\n", caps);
        
        DWORD addr = 0; sz = sizeof(addr);
        if (RegQueryValueExA(hDev, "Address", NULL, &type, (PBYTE)&addr, &sz) == ERROR_SUCCESS)
            printf("  Address (func): %d\n", addr);
        
        RegCloseKey(hDev);
    } else {
        printf("  Cannot open device key: %d\n", ret);
    }

    /* Read LogConf */
    printf("\n--- LogConf Resources ---\n");
    char logConfPath[1024];
    _snprintf_s(logConfPath, sizeof(logConfPath), _TRUNCATE, "%s\\LogConf", devPath);
    
    HKEY hLogConf = NULL;
    ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, logConfPath, 0, KEY_READ, &hLogConf);
    if (ret == ERROR_SUCCESS) {
        for (int vi = 0; ; vi++) {
            CHAR valName[256] = {0};
            DWORD valNameSz = sizeof(valName);
            UCHAR valData[8192] = {0};
            DWORD valDataSz = sizeof(valData);
            DWORD valType = 0;
            
            ret = RegEnumValueA(hLogConf, vi, valName, &valNameSz, NULL, &valType, valData, &valDataSz);
            if (ret != ERROR_SUCCESS) break;
            
            if (valType == REG_BINARY || valType == REG_RESOURCE_LIST || valType == REG_RESOURCE_REQUIREMENTS_LIST) {
                DumpBinary(valName, valData, valDataSz);
            } else {
                printf("  %s: type=%d size=%d (not binary)\n", valName, valType, valDataSz);
            }
        }
        RegCloseKey(hLogConf);
    } else {
        printf("  No LogConf key: %d\n", ret);
        /* Maybe it's directly under device */
        printf("  Checking device key values...\n");
        HKEY hDev2 = NULL;
        ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, devPath, 0, KEY_READ, &hDev2);
        if (ret == ERROR_SUCCESS) {
            for (int vi = 0; ; vi++) {
                CHAR valName[256] = {0};
                DWORD valNameSz = sizeof(valName);
                UCHAR valData[8192] = {0};
                DWORD valDataSz = sizeof(valData);
                DWORD valType = 0;
                
                ret = RegEnumValueA(hDev2, vi, valName, &valNameSz, NULL, &valType, valData, &valDataSz);
                if (ret != ERROR_SUCCESS) break;
                
                if (valType == REG_BINARY && valDataSz >= 8) {
                    DumpBinary(valName, valData, valDataSz);
                }
            }
            RegCloseKey(hDev2);
        }
    }

    /* Also check device parameters */
    printf("\n--- Device Parameters ---\n");
    char paramsPath[1024];
    _snprintf_s(paramsPath, sizeof(paramsPath), _TRUNCATE, "%s\\Device Parameters", devPath);
    
    HKEY hParams = NULL;
    ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, paramsPath, 0, KEY_READ, &hParams);
    if (ret == ERROR_SUCCESS) {
        for (int vi = 0; ; vi++) {
            CHAR valName[256] = {0};
            DWORD valNameSz = sizeof(valName);
            UCHAR valData[4096] = {0};
            DWORD valDataSz = sizeof(valData);
            DWORD valType = 0;
            
            ret = RegEnumValueA(hParams, vi, valName, &valNameSz, NULL, &valType, valData, &valDataSz);
            if (ret != ERROR_SUCCESS) break;
            
            DWORD dw = 0;
            if (valType == REG_DWORD) dw = *(DWORD *)valData;
            printf("  %s = 0x%08X (%d)\n", valName, dw, dw);
        }
        RegCloseKey(hParams);
    } else {
        printf("  No Device Parameters: %d\n", ret);
    }

    printf("\nDone.\n");
    return 0;
}

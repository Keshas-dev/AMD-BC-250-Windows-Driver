#include <windows.h>
#include <stdio.h>
#include <SetupAPI.h>
#include <CfgMgr32.h>
#include <devguid.h>
#include <initguid.h>
#include <PowrProf.h>
#include <string.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "CfgMgr32.lib")
#pragma comment(lib, "PowrProf.lib")

int main(void) {
    printf("AMD BC-250 Device Power Status Check\n");
    printf("=====================================\n\n");

    /* Find BC-250 via SetupAPI */
    HDEVINFO hDevInfo = SetupDiGetClassDevsEx(
        NULL, NULL, NULL,
        DIGCF_PRESENT | DIGCF_ALLCLASSES,
        NULL, NULL, NULL);

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        printf("SetupDiGetClassDevsEx failed: %d\n", GetLastError());
        return 1;
    }

    SP_DEVINFO_DATA devData = {0};
    devData.cbSize = sizeof(SP_DEVINFO_DATA);
    int found = 0;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); i++) {
        CHAR devId[512] = {0};
        DWORD sz = 0;
        if (!SetupDiGetDeviceInstanceIdA(hDevInfo, &devData, devId, sizeof(devId), &sz))
            continue;

        if (strstr(devId, "13FE") != NULL) {
            found = 1;
            printf("Device: %s\n\n", devId);

            /* Get status */
            ULONG status = 0, problem = 0;
            CONFIGRET cr = CM_Get_DevNode_Status(&status, &problem, devData.DevInst, 0);
            if (cr == CR_SUCCESS) {
                printf("--- CM_Get_DevNode_Status ---\n");
                printf("  Status:  0x%08X\n", status);
                printf("  Problem: %d\n", problem);
                
                /* Decode status flags */
                printf("  Flags:\n");
                printf("    DN_STARTED        = %d\n", (status & DN_STARTED) ? 1 : 0);
                printf("    DN_DRIVER_LOADED  = %d\n", (status & DN_DRIVER_LOADED) ? 1 : 0);
                printf("    DN_ENUM_LOADED    = %d\n", (status & 0x00020000) ? 1 : 0);
                printf("    DN_DEVICE_DISABLED = %d\n", (status & 0x00001000) ? 1 : 0);
                printf("    DN_REMOVABLE       = %d\n", (status & 0x04000000) ? 1 : 0);
                printf("    DN_DISABLEABLE     = %d\n", (status & 0x00800000) ? 1 : 0);
            }

            /* Get power state capabilities via registry */
            printf("\n--- Device Registry Info ---\n");
            CHAR regPath[1024];
            _snprintf_s(regPath, sizeof(regPath), _TRUNCATE,
                "SYSTEM\\CurrentControlSet\\Enum\\%s\\Device Parameters",
                devId);
            
            HKEY hKey = NULL;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                DWORD val = 0;
                DWORD valSz = sizeof(val);
                DWORD valType = 0;
                
                if (RegQueryValueExA(hKey, "EnableUlps", NULL, &valType, (PBYTE)&val, &valSz) == ERROR_SUCCESS)
                    printf("  EnableUlps = %d\n", val);
                
                if (RegQueryValueExA(hKey, "DisableComputeQueue", NULL, &valType, (PBYTE)&val, &valSz) == ERROR_SUCCESS)
                    printf("  DisableComputeQueue = %d\n", val);
                
                RegCloseKey(hKey);
            }

            /* Read device properties via SetupAPI */
            printf("\n--- SetupAPI Device Properties ---\n");
            
            /* Try CM_Get_DevNode_Registry_Property for device state */
            
            /* Check if device is in D3 cold state */
            // Use CM_Get_DevNode_Registry_Property to check power state
            
            /* Power state query via registry */
            _snprintf_s(regPath, sizeof(regPath), _TRUNCATE,
                "SYSTEM\\CurrentControlSet\\Enum\\%s",
                devId);
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                for (int vi = 0; ; vi++) {
                    CHAR valName[256] = {0};
                    DWORD valNameSz = sizeof(valName);
                    UCHAR valData[4096] = {0};
                    DWORD valDataSz = sizeof(valData);
                    DWORD valType = 0;
                    
                    LONG ret = RegEnumValueA(hKey, vi, valName, &valNameSz, 
                        NULL, &valType, valData, &valDataSz);
                    if (ret != ERROR_SUCCESS) break;
                    
                    DWORD dwVal = 0;
                    if (valType == REG_DWORD) dwVal = *(DWORD *)valData;
                    
                    if (strcmp(valName, "Capabilities") == 0)
                        printf("  Capabilities = 0x%08X\n", dwVal);
                    else if (strcmp(valName, "ConfigFlags") == 0)
                        printf("  ConfigFlags = 0x%08X\n", dwVal);
                    else if (strcmp(valName, "Address") == 0)
                        printf("  Address (func) = 0x%08X\n", dwVal);
                    else if (strcmp(valName, "UINumber") == 0)
                        printf("  UINumber = %d\n", dwVal);
                    else if (strcmp(valName, "LocationInformation") == 0)
                        printf("  Location = %s\n", valData);
                    else if (strcmp(valName, "ClassGUID") == 0)
                        printf("  ClassGUID = %s\n", valData);
                    else if (strcmp(valName, "Service") == 0)
                        printf("  Service = %s\n", valData);
                }
                RegCloseKey(hKey);
            }

            /* Try to power on the device using CM_Reenumerate_DevNode or similar */
            printf("\n--- Power On Attempt ---\n");
            
            /* Method 1: Try CM_Enable_DevNode */
            cr = CM_Enable_DevNode(devData.DevInst, 0);
            printf("  CM_Enable_DevNode: %s (0x%X)\n", cr == CR_SUCCESS ? "SUCCESS" : "FAILED", cr);

            /* Method 2: Try to reenumerate */
            cr = CM_Reenumerate_DevNode(devData.DevInst, 0);
            printf("  CM_Reenumerate_DevNode: %s (0x%X)\n", cr == CR_SUCCESS ? "SUCCESS" : "FAILED", cr);

            /* Method 3: Query power state via CM_Get_Device_Resources */
            ULONG bufSz = 0;
            cr = CM_Get_Device_Resources_Ex(&bufSz, NULL, devData.DevInst, 0, NULL);
            if (cr == CR_BUFFER_SMALL && bufSz > 0) {
                PBYTE resBuf = (PBYTE)malloc(bufSz);
                if (resBuf) {
                    cr = CM_Get_Device_Resources_Ex(&bufSz, resBuf, devData.DevInst, 0, NULL);
                    if (cr == CR_SUCCESS) {
                        printf("  Device resources: %d bytes\n", bufSz);
                        PPM_RESOURCE_LIST rl = (PPM_RESOURCE_LIST)resBuf;
                        printf("  Resource count: %d\n", rl->Count);
                    } else {
                        printf("  CM_Get_Device_Resources_Ex failed: 0x%X\n", cr);
                    }
                    free(resBuf);
                }
            } else {
                printf("  CM_Get_Device_Resources_Ex: bufSz=%d, cr=0x%X\n", bufSz, cr);
            }

            /* Method 4: Check if we can use CreateFile to open the device PDO */
            printf("\n--- PDO Access ---\n");
            CHAR pdoPath[1024];
            _snprintf_s(pdoPath, sizeof(pdoPath), _TRUNCATE,
                "\\\\.\\%s", devId);
            
            HANDLE hPdo = CreateFileA(pdoPath, 0, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (hPdo != INVALID_HANDLE_VALUE) {
                printf("  PDO opened successfully!\n");
                CloseHandle(hPdo);
            } else {
                printf("  Cannot open PDO: %d\n", GetLastError());
                
                /* Try with different path format */
                _snprintf_s(pdoPath, sizeof(pdoPath), _TRUNCATE,
                    "\\\\?\\%s", devId);
                hPdo = CreateFileA(pdoPath, 0, 0, NULL, OPEN_EXISTING, 0, NULL);
                if (hPdo != INVALID_HANDLE_VALUE) {
                    printf("  PDO opened via \\\\?\\ path!\n");
                    CloseHandle(hPdo);
                } else {
                    printf("  Cannot open via \\\\?\\ either: %d\n", GetLastError());
                }
            }

            break;
        }
    }

    if (!found) {
        printf("BC-250 (13FE) not found!\n");
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    printf("\nDone.\n");
    return found ? 0 : 1;
}

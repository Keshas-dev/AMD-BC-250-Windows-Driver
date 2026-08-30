/* res-dump.c
 *
 * Dumps the assigned HARDWARE RESOURCES (memory ranges) of a PCI device
 * via Configuration Manager APIs (user-mode, reliable, no kernel paths).
 * Usage: res-dump.exe <instance-id substring, e.g. 143E>
 */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <cfgmgr32.h>
#pragma comment(lib, "cfgmgr32.lib")

static void DumpResList(DEVINST dn) {
    LOG_CONF logConf = 0;
    /* try ALLOC first, then BOOT (DeviceTree-style fallback) */
    if (CM_Get_First_Log_Conf(&logConf, dn, ALLOC_LOG_CONF) != CR_SUCCESS || !logConf) {
        if (CM_Get_First_Log_Conf(&logConf, dn, BOOT_LOG_CONF) != CR_SUCCESS || !logConf) {
            printf("  (no allocated/boot log conf)\n");
            return;
        }
        printf("  [BOOT log conf]\n");
    }
    RES_DES rd = logConf;
    while (rd) {
        RES_DES next = 0;
        ULONG size = 0;
        if (CM_Get_Next_Res_Des(&next, rd, ResType_Mem, &size, 0) == CR_SUCCESS) {
            MEM_RESOURCE* mr = (MEM_RESOURCE*)malloc(size);
            if (mr && CM_Get_Res_Des_Data(next, mr, size, 0) == CR_SUCCESS) {
                ULONGLONG start = mr->MEM_Header.MD_Alloc_Base;
                ULONGLONG end   = mr->MEM_Header.MD_Alloc_End;
                printf("  MEM : PA=0x%08X%08X len=0x%08X (%u KB)\n",
                       (unsigned)(end >> 32), (unsigned)start,
                       (unsigned)(end - start + 1),
                       (unsigned)((end - start + 1) / 1024));
            }
            free(mr);
            if (rd != logConf) CM_Free_Res_Des_Handle(rd);
            rd = next;
        } else {
            /* try IRQ/port just in case, then stop */
            break;
        }
    }
    CM_Free_Log_Conf_Handle(logConf);
}

int main(int argc, char* argv[]) {
    const char* needle = (argc > 1) ? argv[1] : "143E";
    ULONG len = 0;
    if (CM_Get_Device_ID_List_SizeA(&len, NULL, CM_GETIDLIST_FILTER_NONE) != CR_SUCCESS || !len) {
        printf("no device list\n"); return 1;
    }
    char* buf = (char*)calloc(len, 1);
    if (!buf || CM_Get_Device_ID_ListA(NULL, buf, len, CM_GETIDLIST_FILTER_NONE) != CR_SUCCESS) {
        printf("list failed\n"); return 1;
    }
    int hits = 0;
    for (char* p = buf; *p; p += strlen(p) + 1) {
        if (!strstr(p, needle)) continue;
        DEVINST dn = 0;
        if (CM_Locate_DevNodeA(&dn, p, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS) {
            printf("Device: %s\n", p);
            DumpResList(dn);
            printf("\n");
            hits++;
        }
    }
    if (!hits) printf("no devices matching '%s'\n", needle);
    free(buf);
    return 0;
}

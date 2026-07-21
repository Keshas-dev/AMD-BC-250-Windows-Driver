#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    DWORD n = EnumSystemFirmwareTables('ACPI', NULL, 0);
    if (n == 0) { printf("EnumSystemFirmwareTables failed (0x%08X)\n", GetLastError()); return 1; }
    BYTE *list = (BYTE*)malloc(n);
    if (EnumSystemFirmwareTables('ACPI', list, n) != n) { printf("enum fail\n"); free(list); return 1; }
    DWORD vfct = 0;
    for (DWORD i = 0; i + 4 <= n; i += 4)
        if (memcmp(list + i, "VFCT", 4) == 0) vfct = *(DWORD*)(list + i);
    if (vfct == 0) { printf("No VFCT table.\n"); free(list); return 1; }

    DWORD size = GetSystemFirmwareTable('ACPI', vfct, NULL, 0);
    BYTE *buf = (BYTE*)malloc(size);
    if (GetSystemFirmwareTable('ACPI', vfct, buf, size) != size) {
        printf("GetSystemFirmwareTable failed (0x%08X)\n", GetLastError()); free(buf); free(list); return 1;
    }
    printf("VFCT: %lu bytes\n", size);

    /* Find the actual ROM: PCI ROM header starts with 0xAA55 signature */
    DWORD romStart = 0, romSize = 0;
    for (DWORD i = 0; i + 0x80 < size; i++) {
        if (buf[i] == 0x55 && buf[i+1] == 0xAA) {
            romSize = *(USHORT*)(buf+i+2) * 512;
            romStart = i;
            printf("ROM @ 0x%lX, size=%lu (0x%lX) bytes\n", romStart, romSize, romSize);
            break;
        }
    }

    /* Save VFCT raw dump */
    HANDLE hf = CreateFileA("bc250-vbios.rom", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) { printf("Save failed (0x%08X)\n", GetLastError()); }
    else {
        DWORD written = 0;
        if (romSize && WriteFile(hf, buf + romStart, romSize, &written, NULL))
            printf("Saved %lu bytes to bc250-vbios.rom\n", written);
        else if (WriteFile(hf, buf, size, &written, NULL))
            printf("Saved %lu bytes to bc250-vbios.rom (full VFCT)\n", written);
        CloseHandle(hf);
    }

    free(buf); free(list);
    return 0;
}

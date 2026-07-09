#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Enumerate ACPI tables, dump VFCT (contains GPU ATOMBIOS / VBIOS image). */
static const char *Sig(DWORD d) {
    static char s[5];
    s[0]=(char)(d&0xFF); s[1]=(char)((d>>8)&0xFF);
    s[2]=(char)((d>>16)&0xFF); s[3]=(char)((d>>24)&0xFF); s[4]=0;
    return s;
}

int main(void) {
    DWORD n = EnumSystemFirmwareTables('ACPI', NULL, 0);
    if (n == 0) { printf("EnumSystemFirmwareTables ACPI failed (0x%08X)\n", GetLastError()); return 1; }
    BYTE *list = (BYTE*)malloc(n);
    if (EnumSystemFirmwareTables('ACPI', list, n) != n) { printf("enum fail\n"); free(list); return 1; }

    DWORD vfct = 0;
    printf("ACPI tables (%lu bytes of signatures):\n", n);
    for (DWORD i = 0; i + 4 <= n; i += 4) {
        DWORD sig = *(DWORD*)(list + i);
        printf("  %s\n", Sig(sig));
        if (memcmp(list + i, "VFCT", 4) == 0) vfct = sig;
    }

    if (vfct == 0) { printf("No VFCT table present on this system.\n"); free(list); return 0; }

    DWORD size = GetSystemFirmwareTable('ACPI', vfct, NULL, 0);
    BYTE *buf = (BYTE*)malloc(size);
    if (GetSystemFirmwareTable('ACPI', vfct, buf, size) != size) {
        printf("GetSystemFirmwareTable VFCT failed (0x%08X)\n", GetLastError()); free(buf); free(list); return 1;
    }
    printf("\nVFCT table: %lu bytes\n", size);
    int atom = 0;
    for (DWORD i = 0; i + 4 < size; i++)
        if (buf[i]=='A'&&buf[i+1]=='T'&&buf[i+2]=='O'&&buf[i+3]=='M') { printf("  ATOM signature at 0x%lX\n", i); atom = 1; }
    for (DWORD i = 0; i < size && i < 256; i += 16) {
        printf("  [0x%06lX] ", i);
        for (int j = 0; j < 16 && (i+j) < size; j++) printf("%02X ", buf[i+j]);
        printf(" ");
        for (int j = 0; j < 16 && (i+j) < size; j++) printf("%c", (buf[i+j]>=32&&buf[i+j]<=126)?buf[i+j]:'.');
        printf("\n");
    }
    if (!atom) printf("  (no ATOM signature in raw VFCT)\n");
    free(buf); free(list);
    return 0;
}

/* vfct-dump.c — read-only ACPI VFCT table (contains GPU VBIOS) via firmware API.
 * Saves raw table to vfct.bin + scans for ATOM BIOS markers in memory. */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    DWORD need = 0;
    UINT tbl = 0;
    enum { ACPI_SIG = 'A' | ('C' << 8) | ('P' << 16) | ('I' << 24) };
    enum { VFCT_SIG = 'V' | ('F' << 8) | ('C' << 16) | ('T' << 24) };
    BYTE *buf = NULL;
    DWORD got = 0;
    FILE *fp = NULL;
    int i, found = 0;

    if (!EnumSystemFirmwareTables(ACPI_SIG, NULL, 0)) {
        printf("EnumSystemFirmwareTables size query gle=%lu\n", GetLastError());
    }
    need = EnumSystemFirmwareTables(ACPI_SIG, NULL, 0);
    printf("ACPI tables total bytes: %lu\n", need);
    if (need > 0 && need < 1024 * 1024) {
        BYTE *list = (BYTE *)malloc(need);
        if (list && EnumSystemFirmwareTables(ACPI_SIG, list, need)) {
            printf("table count: %lu\n", need / 4);
            for (i = 0; i + 4 <= (int)need; i += 4) {
                UINT s = *(UINT *)(list + i);
                printf("  %c%c%c%c%s\n",
                    (char)(s & 0xFF), (char)((s >> 8) & 0xFF),
                    (char)((s >> 16) & 0xFF), (char)((s >> 24) & 0xFF),
                    (s == VFCT_SIG) ? "  <-- VFCT" : "");
                if (s == VFCT_SIG) tbl = 1;
            }
        }
        free(list);
    }
    if (!tbl) {
        printf("NO VFCT table exposed. Done.\n");
        return 1;
    }
    need = GetSystemFirmwareTable(ACPI_SIG, VFCT_SIG, NULL, 0);
    printf("VFCT size: %lu\n", need);
    if (need == 0 || need > 4 * 1024 * 1024) {
        printf("VFCT size bogus, abort.\n");
        return 1;
    }
    buf = (BYTE *)malloc(need);
    if (!buf) return 1;
    got = GetSystemFirmwareTable(ACPI_SIG, VFCT_SIG, buf, need);
    printf("VFCT read: %lu bytes\n", got);
    if (got != need) { free(buf); return 1; }

    fp = fopen("vfct.bin", "wb");
    if (fp) { fwrite(buf, 1, got, fp); fclose(fp); printf("saved vfct.bin\n"); }

    /* scan for ATOM BIOS markers */
    for (i = 0; i + 8 <= (int)got; i++) {
        if (buf[i] == 'A' && buf[i+1] == 'T' && buf[i+2] == 'O' && buf[i+3] == 'M') {
            printf("ATOM magic @0x%X\n", i);
            found++;
        }
        if (buf[i] == '1' && buf[i+1] == '1' && buf[i+2] == '3' && buf[i+3] == '-') {
            char tmp[32];
            int n = (int)got - i < 31 ? (int)got - i : 31;
            memcpy(tmp, buf + i, n);
            tmp[n] = 0;
            printf("113- string @0x%X: %s\n", i, tmp);
            found++;
        }
        if (buf[i] == 0x55 && buf[i+1] == 0xAA && (i % 512) == 0) {
            printf("55AA ROM header @0x%X\n", i);
            found++;
        }
    }
    printf("markers found: %d\n", found);
    free(buf);
    return found ? 0 : 2;
}

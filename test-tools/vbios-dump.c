#include <windows.h>
#include <stdio.h>
#include <string.h>

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
    for (DWORD i = 0; i + 4 <= n; i += 4)
        if (memcmp(list + i, "VFCT", 4) == 0) vfct = *(DWORD*)(list + i);
    if (vfct == 0) { printf("No VFCT table.\n"); free(list); return 0; }

    DWORD size = GetSystemFirmwareTable('ACPI', vfct, NULL, 0);
    BYTE *buf = (BYTE*)malloc(size);
    if (GetSystemFirmwareTable('ACPI', vfct, buf, size) != size) {
        printf("GetSystemFirmwareTable VFCT failed (0x%08X)\n", GetLastError()); free(buf); free(list); return 1;
    }
    printf("VFCT: %lu bytes\n", size);

    /* Locate all "ATOM" occurrences (ROM header + subtables + version strings) */
    printf("\n'ATOM' occurrences:\n");
    for (DWORD i = 0; i + 4 < size; i++)
        if (memcmp(buf+i,"ATOM",4)==0) {
            printf("  0x%lX: ", i);
            for (int j=0;j<16 && i+j<size;j++) printf("%c",(buf[i+j]>=32&&buf[i+j]<=126)?buf[i+j]:'.');
            printf("\n");
        }

    /* Value scan: find the VRAM carve-out size encoded as a LE dword.
       0x02000000 = 512MB, 0x01000000 = 256MB, 0x80000000 = 2GB, 0x200=512, 0x100=256 */
    printf("\n-- size-value scan (LE dword across whole VFCT) --\n");
    /* NOTE hex powers: 0x100=256, 0x200=512, 0x100000=1MB,
       0x1000000=16MB, 0x2000000=32MB, 0x10000000=256MB,
       0x20000000=512MB, 0x80000000=2GB */
    DWORD targets[] = { 0x20000000u, 0x10000000u, 0x02000000u, 0x01000000u, 0x80000000u, 0x200u, 0x100u };
    const char *tname[] = { "512MB (0x20000000)","256MB (0x10000000)",
                           "32MB (0x02000000)","16MB (0x01000000)",
                           "2GB (0x80000000)","512 (0x200)","256 (0x100)" };
    for (int ti = 0; ti < 5; ti++) {
        int hits = 0;
        for (DWORD i = 0; i + 4 <= size; i++) {
            if (*(DWORD*)(buf + i) == targets[ti]) {
                if (hits < 8) printf("  %s @ 0x%lX (0x%08X)\n", tname[ti], i, targets[ti]);
                hits++;
            }
        }
        if (hits) printf("  -> %d hit(s) for %s\n", hits, tname[ti]);
        /* Dump neighborhood around the 512MB total carve-out hit */
        if (targets[ti] == 0x20000000u && hits) {
            for (DWORD i = 0; i < size && i < 0xCBE7+0x40; i++) {
                if (*(DWORD*)(buf+i) == 0x20000000u) {
                    DWORD base = (i > 0x20) ? i-0x20 : 0;
                    printf("  -- context @ 0x%lX (512MB carve-out) --\n", i);
                    for (DWORD j = base; j < size && j < base+0x60; j += 16) {
                        printf("    [0x%lX] ", j);
                        for (int k = 0; k < 16 && j+k < size; k++) printf("%02X ", buf[j+k]);
                        printf(" ");
                        for (int k = 0; k < 16 && j+k < size; k++) printf("%c",(buf[j+k]>=32&&buf[j+k]<=126)?buf[j+k]:'.');
                        printf("\n");
                    }
                    break;
                }
            }
        }
    }

    /* Best-effort: at the first 'ATOM' that looks like a ROM header
       (table-size ushort at +0x06 is plausible), dump 24 ULONGs. */
    for (DWORD i = 0; i + 0x80 < size; i++) {
        if (memcmp(buf+i,"ATOM",4)!=0) continue;
        DWORD tsz = *(USHORT*)(buf+i+0x06);
        if (tsz < 0x40 || tsz > 0x4000) continue;   /* plausible table size */
        printf("\n-- plausible ROM header @ 0x%lX (tableSize=0x%lX) --\n", i, tsz);
        for (int k = 0; k < 24; k++) {
            DWORD v = *(DWORD*)(buf + i + k*4);
            printf("  0x%02X: 0x%08X (%u)\n", k*4, v, v);
        }
        break;
    }

    free(buf); free(list);
    return 0;
}

#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *Sig(DWORD d) {
    static char s[5];
    s[0]=(char)(d&0xFF); s[1]=(char)((d>>8)&0xFF);
    s[2]=(char)((d>>16)&0xFF); s[3]=(char)((d>>24)&0xFF); s[4]=0;
    return s;
}
static int Has(BYTE *b, DWORD n, const char *m) {
    DWORD l = (DWORD)strlen(m);
    for (DWORD i=0; i+l<=n; i++) if (memcmp(b+i,m,l)==0) return 1;
    return 0;
}

int main(void) {
    DWORD n = EnumSystemFirmwareTables('ACPI', NULL, 0);
    if (!n) { printf("enum fail (0x%08X)\n", GetLastError()); return 1; }
    BYTE *list = (BYTE*)malloc(n);
    if (EnumSystemFirmwareTables('ACPI', list, n) != n) { printf("enum fail2\n"); free(list); return 1; }

    printf("=== ACPI tables (%lu sig bytes) ===\n", n);
    for (DWORD i=0; i+4<=n; i+=4) {
        DWORD sig = *(DWORD*)(list+i);
        DWORD sz = GetSystemFirmwareTable('ACPI', sig, NULL, 0);
        printf("  %s  size=%lu\n", Sig(sig), sz);
    }

    /* Dump a preview of each table */
    for (DWORD i=0; i+4<=n; i+=4) {
        DWORD sig = *(DWORD*)(list+i);
        DWORD sz = GetSystemFirmwareTable('ACPI', sig, NULL, 0);
        if (!sz || sz > 2000000) continue;
        BYTE *b = (BYTE*)malloc(sz);
        if (GetSystemFirmwareTable('ACPI', sig, b, sz) != sz) { free(b); continue; }
        int interesting = Has(b,sz,"ATOM")||Has(b,sz,"BIOS")||Has(b,sz,"AMD")
                        ||Has(b,sz,"EDID")||Has(b,sz,"GPU")||Has(b,sz,"VGA");
        printf("\n--- %s (%lu B)%s ---\n", Sig(sig), sz, interesting?" [interesting]":"");
        DWORD lim = sz<160?sz:160;
        for (DWORD o=0; o<lim; o+=16) {
            printf("  [0x%06lX] ", o);
            for (int j=0;j<16 && o+j<lim;j++) printf("%02X ", b[o+j]);
            printf(" ");
            for (int j=0;j<16 && o+j<lim;j++) printf("%c",(b[o+j]>=32&&b[o+j]<=126)?b[o+j]:'.');
            printf("\n");
        }
        free(b);
    }
    free(list);
    return 0;
}

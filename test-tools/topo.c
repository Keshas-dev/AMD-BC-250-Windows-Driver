#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD GetAcpi(DWORD sig, BYTE **out) {
    DWORD n = EnumSystemFirmwareTables('ACPI', NULL, 0);
    if (!n) return 0;
    BYTE *list = (BYTE*)malloc(n);
    if (EnumSystemFirmwareTables('ACPI', list, n) != n) { free(list); return 0; }
    DWORD found = 0;
    for (DWORD i = 0; i + 4 <= n; i += 4)
        if (*(DWORD*)(list + i) == sig) found = sig;
    free(list);
    if (!found) return 0;
    DWORD sz = GetSystemFirmwareTable('ACPI', sig, NULL, 0);
    BYTE *b = (BYTE*)malloc(sz);
    if (GetSystemFirmwareTable('ACPI', sig, b, sz) != sz) { free(b); return 0; }
    *out = b; return sz;
}
static const char *CratType(UCHAR t) {
    switch (t) {
        case 0: return "Processor(CPU)";
        case 1: return "Memory";
        case 2: return "Cache";
        case 3: return "Generic/Accelerator";
        case 4: return "GenericInitiator";
        default: return "?";
    }
}

int main(void) {
    /* ---- CDIT ---- */
    BYTE *cd = NULL; DWORD cdsz = GetAcpi('TIDC', &cd);   /* 'CDIT' LE */
    if (cdsz) {
        printf("=== CDIT (%lu B) ===\n", cdsz);
        for (DWORD i = 0; i < cdsz; i += 16) {
            printf("  [0x%03lX] ", i);
            for (DWORD j = 0; j < 16 && i+j < cdsz; j++) printf("%02X ", cd[i+j]);
            printf(" ");
            for (DWORD j = 0; j < 16 && i+j < cdsz; j++) printf("%c",(cd[i+j]>=32&&cd[i+j]<=126)?cd[i+j]:'.');
            printf("\n");
        }
        /* best-effort: after 36B header, numDomains(USHORT) + (from,to,dist) */
        if (cdsz > 38) {
            DWORD nd = *(USHORT*)(cd + 36);
            printf("  hdr-payload: numDomains(off36)=%u\n", nd);
            for (DWORD k = 38; k + 4 <= cdsz; k += 4) {
                DWORD v = *(DWORD*)(cd + k);
                printf("    off0x%lX: 0x%08X (from=%u to=%u dist=%u)\n", k, v, v&0xFF, (v>>8)&0xFF, (v>>16)&0xFFFF);
            }
        }
        free(cd);
    } else printf("CDIT not found.\n");

    /* ---- CRAT: walk subtables (u8 type; u8 length) ---- */
    BYTE *cr = NULL; DWORD crsz = GetAcpi('TARC', &cr);   /* 'CRAT' LE */
    if (crsz) {
        printf("\n=== CRAT (%lu B) ===\n", crsz);
        printf("  acpi-header len=0x%lX (36B); first entry region @0x24 = %u (count field)\n", (*(DWORD*)(cr+4)), *(DWORD*)(cr+0x24));
        int ec[256] = {0};
        DWORD p = 0x30;   /* first subtable after count field */
        while (p + 2 <= crsz) {
            UCHAR type = cr[p];
            UCHAR len  = cr[p+1];
            if (type == 0 && len == 0) { printf("  [zero padding @0x%lX] populated region ends\n", p); break; }
            if (len < 8 || p + len > crsz) { printf("  [bad entry @0x%lX type=%u len=%u] stop\n", p, type, len); break; }
            ec[type]++;
            printf("\n  entry @0x%lX type=%u(%s) len=%u\n", p, type, CratType(type), len);
            for (DWORD i = p; i < p + len; i += 16) {
                printf("    [0x%04lX] ", i);
                for (DWORD j = 0; j < 16 && i+j < p+len; j++) printf("%02X ", cr[i+j]);
                printf(" ");
                for (DWORD j = 0; j < 16 && i+j < p+len; j++) printf("%c",(cr[i+j]>=32&&cr[i+j]<=126)?cr[i+j]:'.');
                printf("\n");
            }
            p += len;
        }
        printf("\n  -- CRAT summary --\n");
        for (int t = 0; t < 256; t++) if (ec[t]) printf("    type %u (%s): %d entries\n", t, CratType((UCHAR)t), ec[t]);
        /* memory-size scan */
        printf("  -- memory-size scan --\n");
        DWORD ms[] = { 0x20000000u, 0x10000000u, 0x80000000u };
        const char *mn[] = { "512MB","256MB","2GB" };
        for (int ti = 0; ti < 3; ti++) {
            int hits = 0;
            for (DWORD i = 0; i + 4 <= crsz; i++)
                if (*(DWORD*)(cr+i) == ms[ti]) hits++;
            if (hits) printf("    %s (0x%08X): %d hit(s)\n", mn[ti], ms[ti], hits);
        }
        free(cr);
    } else printf("\nCRAT not found.\n");
    return 0;
}

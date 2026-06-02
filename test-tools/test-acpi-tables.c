#include <windows.h>
#include <stdio.h>

int main(void) {
    printf("ACPI Firmware Table Enumerator\n");
    printf("=============================\n\n");

    /* Enumerate all firmware tables */
    UINT8 buf[4096];
    UINT32 sz = EnumSystemFirmwareTables('ACPI', buf, sizeof(buf));
    if (sz > 0) {
        printf("ACPI tables found (%d bytes):\n", sz);
        DWORD count = sz / sizeof(UINT32);
        for (DWORD i = 0; i < count; i++) {
            UINT32 id = *(UINT32 *)(buf + i * 4);
            char idStr[5] = {(char)(id & 0xFF), (char)((id>>8)&0xFF), 
                            (char)((id>>16)&0xFF), (char)((id>>24)&0xFF), 0};
            
            /* Try to read the table */
            UINT32 tableSz = GetSystemFirmwareTable(id, 0, NULL, 0);
            printf("  [%d] '%s' (0x%08X): %d bytes", i, idStr, id, tableSz);
            
            if (tableSz > 0 && tableSz <= sizeof(buf)) {
                if (GetSystemFirmwareTable(id, 0, buf, tableSz) == tableSz) {
                    /* Print header info */
                    char oemId[7] = {0};
                    char oemTableId[9] = {0};
                    char creatorId[5] = {0};
                    memcpy(oemId, buf + 10, 6);
                    memcpy(oemTableId, buf + 16, 8);
                    memcpy(creatorId, buf + 24, 4);
                    
                    printf(" OEM='%s' OEM_TABLE='%s' CREATOR='%s' REV=%d",
                        oemId, oemTableId, creatorId, buf[8]);
                    
                    /* For MCFG, parse entries */
                    if (idStr[0] == 'M' && idStr[1] == 'C' && idStr[2] == 'F' && idStr[3] == 'G') {
                        printf("\n    MCFG entries:");
                        UINT32 entries = (tableSz - 36) / 16;
                        for (UINT32 e = 0; e < entries; e++) {
                            UINT32 base = *(UINT32 *)(buf + 36 + e * 16);
                            UINT16 seg = *(UINT16 *)(buf + 36 + e * 16 + 4);
                            UCHAR sBus = *(UCHAR *)(buf + 36 + e * 16 + 6);
                            UCHAR eBus = *(UCHAR *)(buf + 36 + e * 16 + 7);
                            printf("\n      [%d] Base=0x%08X Seg=%d Buses=%d-%d", 
                                e, base, seg, sBus, eBus);
                        }
                    }
                }
            } else if (tableSz == 0) {
                printf(" (GetLastError=%d)", GetLastError());
            } else {
                printf(" (too large: %d)", tableSz);
            }
            printf("\n");
        }
    } else {
        printf("EnumSystemFirmwareTables('ACPI') failed: %d\n\n", GetLastError());
        
        /* Try with 'RSDT' directly */
        UINT32 rsdtSz = GetSystemFirmwareTable('RSDT', 0, NULL, 0);
        printf("Trying RSDT directly: %d bytes (err=%d)\n", rsdtSz, GetLastError());
        
        UINT32 xsdtSz = GetSystemFirmwareTable('XSDT', 0, NULL, 0);
        printf("Trying XSDT directly: %d bytes (err=%d)\n", xsdtSz, GetLastError());
        
        /* Try individual table names */
        const char *tables[] = {
            "APIC", "ASF!", "BERT", "BGRT", "BIOS", "BOOT", "CLID", "CPEP",
            "CRAT", "CSRT", "DBGP", "DBG2", "DCBT", "DMAR", "DRTM", "DSDT",
            "ECDT", "EINJ", "ERST", "FACP", "FACS", "FPDT", "GTDT", "HEST",
            "HPET", "IBFT", "IORT", "IVRS", "LPIT", "MCFG", "MCHI",
            "MPST", "MSCT", "MSDM", "MTMR", "NFIT", "OEMx", "PCCT", "PDTT",
            "PHAT", "PMTT", "PPT", "PRMT", "RASF", "RGRT", "SDEV", "SLIC",
            "SLIT", "SPCR", "SPMI", "SRAT", "STAO", "SVKL", "TCPA", "TPM2",
            "UEFI", "VHPE", "WAET", "WDAT", "WDRT", "WERB", "WPPT", "WSMT",
            "XENV"
        };
        
        printf("\nTrying individual table names:\n");
        for (int i = 0; i < sizeof(tables)/sizeof(tables[0]); i++) {
            UINT32 fourcc = tables[i][0] | (tables[i][1] << 8) | (tables[i][2] << 16) | (tables[i][3] << 24);
            UINT32 sz2 = GetSystemFirmwareTable(fourcc, 0, NULL, 0);
            if (sz2 > 0) {
                printf("  '%s': %d bytes\n", tables[i], sz2);
            }
        }
    }

    /* Also try 'FIRM' tables */
    printf("\n--- Raw firmware provider ---\n");
    sz = EnumSystemFirmwareTables('FIRM', buf, sizeof(buf));
    if (sz > 0) {
        printf("FIRM tables found: %d bytes\n", sz);
    } else {
        printf("No FIRM tables: %d\n", GetLastError());
    }

    printf("\nDone.\n");
    return 0;
}

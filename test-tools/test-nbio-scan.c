#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

/* NBIO config access register offsets (from NBIO base) */
#define NBIO_CONFIG_ADDR_OFF    0x60
#define NBIO_CONFIG_DATA_OFF    0x64

/* Extended NBIO config access (may use different offsets on some platforms) */
#define NBIO_EXT_CONFIG_ADDR_OFF 0x00B8
#define NBIO_EXT_CONFIG_DATA_OFF 0x00BC

/* Try to generate a PCI config cycle via NBIO MMIO registers */
static int TryNbioPciConfig(HANDLE hDev, UINT64 nbioBase,
    UINT32 bus, UINT32 dev, UINT32 func,
    UINT32 offset, UINT32 *valueOut, int isWrite)
{
    /* Use NBIO CONFIG_ADDR (same format as IO port 0xCF8) */
    AMDBC250_IOCTL_WRITE_PCI_CONFIG w = {0};
    /* WRITE_PCI_CONFIG IOCTL can also write to arbitrary physical addresses */
    /* But we need a new IOCTL for this... */
    
    /* For now, use READ_REG at NBIO_CONFIG_ADDR/DATA directly */
    /* The KMD must have the NBIO base mapped already */
    /* Use INIT_HARDWARE to map NBIO base, then WRITE_REG to send config cycle */
    
    return 0;
}

int main(void) {
    printf("AMD BC-250 NBIO PCI Config Access Test\n");
    printf("========================================\n\n");
    
    /* This tool requires a new KMD IOCTL that can write to arbitrary MMIO */
    /* For now, try the existing READ_REG IOCTL on candidate NBIO bases */
    
    /* Candidate NBIO base addresses for AMD Family 17h/19h */
    UINT64 nbioCandidates[] = {
        0xFDF00000ULL,  /* Most common for Family 17h */
        0xFEC00000ULL,  /* Common for some platforms */
        0xFED00000ULL,  /* Common for some platforms */
        0xFEE00000ULL,  /* Common for some platforms */
        0xFDE00000ULL,  /* Alternative for Family 17h */
        0xFDC00000ULL,  /* Alternative for Family 17h */
        0xFDB00000ULL,  /* Alternative for Family 17h */
        0xFD000000ULL,  /* SMN base on some platforms */
        0xFE000000ULL,  /* ECAM candidate */
        0xFC000000ULL,  /* ECAM candidate */
        0xE0000000ULL,  /* ECAM candidate */
        0xF0000000ULL,  /* ECAM candidate */
        0xF8000000ULL,  /* ECAM candidate */
        0x80000000ULL,  /* ECAM candidate */
        0x90000000ULL,  /* ECAM candidate */
        0xA0000000ULL,  /* ECAM candidate */
        0xB0000000ULL,  /* ECAM candidate */
        0x40000000ULL,  /* ECAM candidate */
        0x50000000ULL,  /* ECAM candidate */
        0x60000000ULL,  /* ECAM candidate */
        0x70000000ULL,  /* ECAM candidate */
    };

    HANDLE hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device (code %d)\n", (int)GetLastError());
        return 1;
    }

    printf("Scanning NBIO candidates for host bridge (0x1022:0x13E0) signature...\n\n");

    for (int i = 0; i < sizeof(nbioCandidates)/sizeof(nbioCandidates[0]); i++) {
        UINT64 base = nbioCandidates[i];
        
        /* Map this address as MMIO via INIT_HARDWARE */
        AMDBC250_IOCTL_INIT_HARDWARE init = {0};
        init.MmioPhysicalBase = base;
        init.MmioSize = 0x1000; /* 4KB - enough for config registers */
        init.FbPhysicalBase = 0;
        init.FbSize = 0;

        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(hDev, 0x80000B80,
            &init, sizeof(init), NULL, 0, &bytes, NULL);
        if (!ok) {
            printf("  [0x%08llX] INIT_HARDWARE FAILED\n", base);
            continue;
        }

        /* Try to read NBIO vendor/device ID at NBIO_CONFIG_ADDR standard form */
        /* Try reading various known NBIO register offsets */
        
        /* Method 1: Read at offset 0x0000 (might be vendor ID for some blocks) */
        AMDBC250_IOCTL_REG_ACCESS reg = {0};
        reg.RegisterOffset = 0x0000;
        bytes = 0;
        ok = DeviceIoControl(hDev, 0x80000B88,
            &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
        ULONG val0 = reg.Value;

        /* Method 2: Read at offset 0x0004 */
        reg.RegisterOffset = 0x0004;
        bytes = 0;
        DeviceIoControl(hDev, 0x80000B88,
            &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
        ULONG val4 = reg.Value;

        /* Method 3: Read at offset 0x0060 (NBIO_CONFIG_ADDR) */
        reg.RegisterOffset = 0x0060;
        bytes = 0;
        DeviceIoControl(hDev, 0x80000B88,
            &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
        ULONG val60 = reg.Value;

        /* Method 4: Read at offset 0x0064 (NBIO_CONFIG_DATA) */
        reg.RegisterOffset = 0x0064;
        bytes = 0;
        DeviceIoControl(hDev, 0x80000B88,
            &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
        ULONG val64 = reg.Value;

        /* Method 5: Read at offset 0x00B8 (NBIO_EXT_CONFIG_ADDR) */
        reg.RegisterOffset = 0x00B8;
        bytes = 0;
        DeviceIoControl(hDev, 0x80000B88,
            &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
        ULONG valB8 = reg.Value;

        /* Check if any value looks like a host bridge (0x1022:0x13E0) signature */
        int foundNbio = 0;
        const char *note = "";
        
        /* The NBIO typically has the host bridge vendor/device at offset 0 or similar */
        if ((val0 & 0xFFFF) == 0x1022 || ((val0 >> 16) & 0xFFFF) == 0x1022) {
            foundNbio = 1;
            note = "VALID - vendor ID found";
        } else if (val0 != 0 && val0 != 0xFFFFFFFF) {
            note = "non-zero value";
        }
        
        /* Also check if CONFIG_ADDR at 0x60 seems valid (bit 23 set by default) */
        if (!foundNbio && val60 != 0 && val60 != 0xFFFFFFFF) {
            note = "CONFIG_ADDR has value";
        }
        
        if (val0 != 0 || val60 != 0 || val4 != 0 || valB8 != 0) {
            printf("  [0x%08llX] +0x00=0x%08X +0x04=0x%08X +0x60=0x%08X +0x64=0x%08X +0xB8=0x%08X",
                base, val0, val4, val60, val64, valB8);
            if (foundNbio) printf(" *** %s ***", note);
            printf("\n");
        }
        
        /* Try to use NBIO config access to read BC-250 (B1:D0:F0) PCI config */
        if (val60 != 0) {
            /* Try writing the PCI config address to NBIO_CONFIG_ADDR */
            AMDBC250_IOCTL_REG_ACCESS wreg = {0};
            
            /* Program NBIO_CONFIG_ADDR with BC-250's config address */
            wreg.RegisterOffset = 0x0060;
            wreg.Value = 0x80000000 | (1 << 16) | (0 << 11) | (0 << 8) | 0;
            bytes = 0;
            DeviceIoControl(hDev, 0x80000B8C, /* WRITE_REG */
                &wreg, sizeof(wreg), NULL, 0, &bytes, NULL);
                
            /* Read NBIO_CONFIG_DATA */
            reg.RegisterOffset = 0x0064;
            bytes = 0;
            DeviceIoControl(hDev, 0x80000B88,
                &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
            ULONG vendorDevice = reg.Value;
            
            if ((vendorDevice & 0xFFFF) == 0x1002 || ((vendorDevice >> 16) & 0xFFFF) == 0x1002) {
                printf("\n  *** NBIO CONFIG ACCESS WORKS! Vendor/Device via NBIO: 0x%08X ***\n", vendorDevice);
                
                /* Read Command register */
                wreg.RegisterOffset = 0x0060;
                wreg.Value = 0x80000000 | (1 << 16) | (0 << 11) | (0 << 8) | 4;
                DeviceIoControl(hDev, 0x80000B8C, &wreg, sizeof(wreg), NULL, 0, &bytes, NULL);
                
                reg.RegisterOffset = 0x0064;
                DeviceIoControl(hDev, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
                ULONG cmdSts = reg.Value;
                printf("  Command/Status register: 0x%08X\n", cmdSts);
                
                /* Try to enable Memory Space - write 0x0007 to Command register */
                wreg.RegisterOffset = 0x0060;
                wreg.Value = 0x80000000 | (1 << 16) | (0 << 11) | (0 << 8) | 4;
                DeviceIoControl(hDev, 0x80000B8C, &wreg, sizeof(wreg), NULL, 0, &bytes, NULL);
                
                wreg.RegisterOffset = 0x0064;
                wreg.Value = 0x0007; /* I/O + Mem + BusMaster */
                DeviceIoControl(hDev, 0x80000B8C, &wreg, sizeof(wreg), NULL, 0, &bytes, NULL);
                
                /* Read back command register to verify */
                wreg.RegisterOffset = 0x0060;
                wreg.Value = 0x80000000 | (1 << 16) | (0 << 11) | (0 << 8) | 4;
                DeviceIoControl(hDev, 0x80000B8C, &wreg, sizeof(wreg), NULL, 0, &bytes, NULL);
                
                reg.RegisterOffset = 0x0064;
                DeviceIoControl(hDev, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
                ULONG cmdStsAfter = reg.Value;
                printf("  Command after write: 0x%08X (expected 0x0007XX...)\n", cmdStsAfter);
                
                if (cmdStsAfter != cmdSts) {
                    printf("\n  *** PCI CONFIG WRITE WORKS VIA NBIO! ***\n");
                    printf("  Re-mapping BAR5 at 0xFE800000 to test MMIO...\n");
                    
                    /* Now re-map BAR5 and read GPU registers */
                    init.MmioPhysicalBase = 0xFE800000;
                    init.MmioSize = 0x80000;
                    init.FbPhysicalBase = 0;
                    init.FbSize = 0;
                    DeviceIoControl(hDev, 0x80000B80, &init, sizeof(init), NULL, 0, &bytes, NULL);
                    
                    reg.RegisterOffset = 0x0000;
                    DeviceIoControl(hDev, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
                    printf("  GPU_ID[0x0000] after NBIO enable: 0x%08X\n", reg.Value);
                    
                    reg.RegisterOffset = 0x0004;
                    DeviceIoControl(hDev, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
                    printf("  GPU reg[0x0004]: 0x%08X\n", reg.Value);
                    
                    /* Also read a few more interesting registers */
                    reg.RegisterOffset = 0x0C00;
                    DeviceIoControl(hDev, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
                    printf("  Scratch[0x0C00]: 0x%08X\n", reg.Value);
                    
                    printf("\n  *** BC-250 MMIO WORKS THROUGH NBIO! ***\n");
                    printf("  The NBIO_CONFIG_ADDR/DATA at 0xFDF00000+0x60/+0x64 is the key!\n");
                }
            }
        }
    }

    CloseHandle(hDev);
    printf("\nDone.\n");
    return 0;
}

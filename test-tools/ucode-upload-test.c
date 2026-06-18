#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define IOCTL_AMDBC250_READ_REG   0x900
#define IOCTL_AMDBC250_WRITE_REG  0x901

static HANDLE g_hGpu = INVALID_HANDLE_VALUE;

static BOOL GpuReadReg(ULONG offset, ULONG *value) {
    ULONG out = 0; DWORD br = 0;
    BOOL ok = DeviceIoControl(g_hGpu, IOCTL_AMDBC250_READ_REG,
        &offset, sizeof(offset), &out, sizeof(out), &br, NULL);
    if (ok && value) *value = out;
    return ok;
}

static BOOL GpuWriteReg(ULONG offset, ULONG value) {
    ULONG params[2] = { offset, value }; DWORD br = 0;
    return DeviceIoControl(g_hGpu, IOCTL_AMDBC250_WRITE_REG,
        params, sizeof(params), NULL, 0, &br, NULL);
}

static BOOL LoadFirmwareDirect(const char *filename, ULONG fwType) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) { printf("  Cannot open %s\n", filename); return FALSE; }
    
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    unsigned char *blob = (unsigned char *)malloc(fileSize);
    if (!blob) { fclose(fp); return FALSE; }
    fread(blob, 1, fileSize, fp);
    fclose(fp);
    
    /* Parse firmware header */
    unsigned int *hdr = (unsigned int *)blob;
    unsigned int hdrSize = hdr[1];
    unsigned int ucodeVersion = hdr[4];
    unsigned int ucodeSize = hdr[5];
    unsigned int ucodeOffset = hdr[6];
    unsigned int jtOffsetDw = (hdrSize >= 44) ? hdr[9] : 0;
    unsigned int jtSizeDw = (hdrSize >= 44) ? hdr[10] : 0;
    
    printf("  Header: hdrSize=%u ucodeVer=0x%X ucodeSize=%u ucodeOff=%u\n",
        hdrSize, ucodeVersion, ucodeSize, ucodeOffset);
    printf("  JT: offsetDw=%u sizeDw=%u\n", jtOffsetDw, jtSizeDw);
    
    if (ucodeSize == 0 || ucodeOffset + ucodeSize > (unsigned long)fileSize) {
        printf("  Invalid ucode fields\n");
        free(blob);
        return FALSE;
    }
    
    /* Select registers based on firmware type */
    ULONG regCntl, regUcodeAddr, regUcodeData;
    ULONG haltBit;
    switch (fwType) {
    case 1: /* ME */
        regCntl = 0x4A74; regUcodeAddr = 0x172B8; regUcodeData = 0x172BC;
        haltBit = (1 << 28);
        break;
    case 2: /* PFP */
        regCntl = 0x4A74; regUcodeAddr = 0x172B0; regUcodeData = 0x172B4;
        haltBit = (1 << 30);
        break;
    case 3: /* CE */
        regCntl = 0x4A74; regUcodeAddr = 0x172C0; regUcodeData = 0x172C4;
        haltBit = (1 << 29);
        break;
    default:
        printf("  Unknown type %u\n", fwType);
        free(blob); return FALSE;
    }
    
    const char *typeName = (fwType == 1) ? "ME" : (fwType == 2) ? "PFP" : "CE";
    printf("\n  Loading %s firmware via UCODE_DATA...\n", typeName);
    
    /* Step 1: Halt target engine */
    ULONG meCntl; GpuReadReg(0x4A74, &meCntl);
    printf("  ME_CNTL before halt: 0x%08X\n", meCntl);
    
    GpuWriteReg(0x4A74, meCntl | haltBit);
    Sleep(10);
    
    GpuReadReg(0x4A74, &meCntl);
    printf("  ME_CNTL after halt:  0x%08X\n", meCntl);
    
    /* Step 2: Reset UCODE_ADDR to 0 */
    GpuWriteReg(regUcodeAddr, 0);
    Sleep(1);
    
    /* Step 3: Upload ucode DWORDs via UCODE_DATA */
    unsigned int *ucodeDw = (unsigned int *)(blob + ucodeOffset);
    unsigned int ucodeDwCount = ucodeSize / 4;
    
    printf("  Uploading %u DWORDs of ucode...\n", ucodeDwCount);
    for (unsigned int i = 0; i < ucodeDwCount; i++) {
        GpuWriteReg(regUcodeData, ucodeDw[i]);
    }
    printf("  Ucode uploaded (%u DWORDs)\n", ucodeDwCount);
    
    /* Step 4: Upload Jump Table (if present) */
    if (jtSizeDw > 0) {
        unsigned int jtByteOff = ucodeOffset + (jtOffsetDw * 4);
        if (jtByteOff + jtSizeDw * 4 <= (unsigned long)fileSize) {
            unsigned int *jtDw = (unsigned int *)(blob + jtByteOff);
            printf("  Uploading %u JT DWORDs...\n", jtSizeDw);
            for (unsigned int i = 0; i < jtSizeDw; i++) {
                GpuWriteReg(regUcodeData, jtDw[i]);
            }
            printf("  JT uploaded\n");
        }
    }
    
    /* Step 5: Write version to UCODE_ADDR to commit */
    GpuWriteReg(regUcodeAddr, ucodeVersion);
    Sleep(10);
    
    /* Read back version */
    ULONG readback; GpuReadReg(regUcodeAddr, &readback);
    printf("  UCODE_ADDR readback: 0x%08X (expected 0x%X)\n", readback, ucodeVersion);
    
    /* Step 6: Unhalt target engine */
    GpuWriteReg(0x4A74, meCntl & ~haltBit);
    Sleep(10);
    
    GpuReadReg(0x4A74, &meCntl);
    printf("  ME_CNTL after load: 0x%08X\n", meCntl);
    
    free(blob);
    return TRUE;
}

int main(int argc, char *argv[]) {
    printf("=== UCODE_DATA Direct Firmware Loader ===\n");
    printf("Uploads entire firmware via UCODE_DATA registers (no IC_BASE DMA)\n\n");
    
    if (argc < 3) {
        printf("Usage: %s <firmware.bin> <type: 1=ME 2=PFP 3=CE>\n", argv[0]);
        printf("       %s all  (loads ME+PFP from firmware directory)\n", argv[0]);
        return 1;
    }
    
    g_hGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_hGpu == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("GPU driver opened\n\n");
    
    /* Check initial state */
    ULONG scratch, meCntl, grbm;
    GpuReadReg(0x32D4, &scratch); GpuReadReg(0x4A74, &meCntl); GpuReadReg(0x3264, &grbm);
    printf("Initial: SCRATCH=0x%08X ME_CNTL=0x%08X GRBM=0x%08X\n\n", scratch, meCntl, grbm);
    
    if (strcmp(argv[1], "all") == 0) {
        /* Load ME then PFP */
        char mePath[MAX_PATH], pfpPath[MAX_PATH];
        GetModuleFileNameA(NULL, mePath, MAX_PATH);
        char *slash = strrchr(mePath, '\\');
        if (slash) { *(slash + 1) = 0; } else { strcpy(mePath, ""); }
        strcpy(pfpPath, mePath);
        strcat(mePath, "..\\..\\firmware\\cyan_skillfish2_me.bin");
        strcat(pfpPath, "..\\..\\firmware\\cyan_skillfish2_pfp.bin");
        
        printf("--- Loading ME ---\n");
        LoadFirmwareDirect(mePath, 1);
        printf("\n--- Loading PFP ---\n");
        LoadFirmwareDirect(pfpPath, 2);
    } else {
        ULONG fwType = (ULONG)atoi(argv[2]);
        printf("--- Loading %s ---\n", (fwType == 1) ? "ME" : (fwType == 2) ? "PFP" : "CE");
        LoadFirmwareDirect(argv[1], fwType);
    }
    
    /* Final state */
    GpuReadReg(0x32D4, &scratch); GpuReadReg(0x4A74, &meCntl);
    printf("\nFinal: SCRATCH=0x%08X ME_CNTL=0x%08X\n", scratch, meCntl);
    
    CloseHandle(g_hGpu);
    return 0;
}

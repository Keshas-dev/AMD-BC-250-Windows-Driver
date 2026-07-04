#include <windows.h>
#include <stdio.h>

#define PSP_DEVICE      L"\\\\.\\AmdBcPsp"
#define PSP_CTL(fn)     CTL_CODE(FILE_DEVICE_UNKNOWN, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_INIT_HW          PSP_CTL(0x803)
#define IOCTL_PSP_READ_REG         PSP_CTL(0x800)
#define IOCTL_PSP_WRITE_REG        PSP_CTL(0x801)
#define IOCTL_PSP_LOAD_IP_FW_DIRECT PSP_CTL(0x824)
#define IOCTL_PSP_SMU_WAKE         PSP_CTL(0x821)
#define IOCTL_PSP_GET_STATUS       PSP_CTL(0x808)
#define GFX_FW_TYPE_CP_ME         1
#define GFX_FW_TYPE_CP_PFP        2
#define GFX_FW_TYPE_CP_CE         3
#define GFX_FW_TYPE_CP_MEC        4
#define GFX_FW_TYPE_CP_MEC1       5
#define GFX_FW_TYPE_RLC_G         8
#define GFX_FW_TYPE_SDMA0         9
#define GFX_FW_TYPE_SDMA1         10

static HANDLE gPsp = INVALID_HANDLE_VALUE;

typedef struct {
    ULONG Message;
    ULONG Argument;
    ULONG Reserved[2];
} SMU_WAKE_REQ;

typedef struct {
    ULONG Message;
    ULONG Argument;
    ULONG Response;
    ULONG Status;
} SMU_WAKE_RESP;

static BOOL SmuWake(ULONG msg, ULONG arg, PULONG resp, PULONG status) {
    SMU_WAKE_REQ req = { msg, arg, {0, 0} };
    SMU_WAKE_RESP out = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_SMU_WAKE, &req, sizeof(req), &out, sizeof(out), &br, NULL);
    if (resp) *resp = out.Response;
    if (status) *status = out.Status;
    return ok;
}

static ULONG ReadReg(ULONG off) {
    ULONG in[2] = {off, 0}, out[2] = {0}, br = 0;
    DeviceIoControl(gPsp, IOCTL_PSP_READ_REG, in, 4, out, 8, &br, NULL);
    return out[0];
}

static void WriteReg(ULONG off, ULONG val) {
    ULONG in[2] = {off, val}, br = 0;
    DeviceIoControl(gPsp, IOCTL_PSP_WRITE_REG, in, 8, NULL, 0, &br, NULL);
}

static BOOL LoadFirmware(ULONG fwType, const char* filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("  Cannot open %s\n", filename);
        return FALSE;
    }
    fseek(fp, 0, SEEK_END);
    long fwSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fwSize <= 0 || fwSize > 1024*1024) {
        printf("  Invalid firmware size %ld\n", fwSize);
        fclose(fp); return FALSE;
    }
    size_t bufSize = 8 + fwSize;
    BYTE *buf = (BYTE*)malloc(bufSize);
    if (!buf) { fclose(fp); return FALSE; }
    *((ULONG*)buf) = fwType;
    *((ULONG*)(buf+4)) = (ULONG)fwSize;
    fread(buf+8, 1, fwSize, fp);
    fclose(fp);

    DWORD ret = 0;
    BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_LOAD_IP_FW_DIRECT, buf, (DWORD)bufSize, NULL, 0, &ret, NULL);
    printf("  Load type=%u size=%ld: %s (err=%lu)\n", fwType, fwSize, ok ? "OK" : "FAIL", GetLastError());
    free(buf);
    return ok;
}

static void PrintStatus(void) {
    typedef struct {
        ULONG C2PMSG_81;
        ULONG C2PMSG_35;
        ULONG C2PMSG_36;
        ULONG PspAlive;
        ULONG FwLoaded;
        ULONG FwSize;
        ULONG FwPaShifted;
        ULONG RingCreated;
    } STATUS_INFO;
    STATUS_INFO st = {0};
    DWORD br = 0;
    DeviceIoControl(gPsp, IOCTL_PSP_GET_STATUS, NULL, 0, &st, sizeof(st), &br, NULL);
    
    printf("  C2PMSG_81=0x%08X C2PMSG_35=0x%08X\n", st.C2PMSG_81, st.C2PMSG_35);
    printf("  PspAlive=%u FwLoaded=%u FwSize=%u RingCreated=%u\n", 
           st.PspAlive, st.FwLoaded, st.FwSize, st.RingCreated);
}

static void PrintSMU(void) {
    printf("  SMU C2PMSG: 0x16A08=0x%08X 0x16A48=0x%08X 0x16A68=0x%08X\n",
           ReadReg(0x16A08), ReadReg(0x16A48), ReadReg(0x16A68));
    printf("  PSP       : 0x16000=0x%08X 0x16004=0x%08X 0x16008=0x%08X\n",
           ReadReg(0x16000), ReadReg(0x16004), ReadReg(0x16008));
    printf("  PSP BOOT  : 0x16038=0x%08X 0x16020=0x%08X 0x16024=0x%08X\n",
           ReadReg(0x16038), ReadReg(0x16020), ReadReg(0x16024));
}

int main(void) {
    gPsp = CreateFileW(PSP_DEVICE, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gPsp == INVALID_HANDLE_VALUE) { printf("FAIL: Cannot open PSP (err=%lu)\n", GetLastError()); return 1; }
    printf("[+] PSP opened\n");

    printf("\n[+] Init HW\n");
    UCHAR req[32] = {0};
    *(ULONGLONG*)(req+0) = 0xFE800000ULL;
    *(ULONG*)(req+8) = 0x80000;
    ULONG out = 0; DWORD br = 0;
    BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_INIT_HW, req, 12, &out, 4, &br, NULL);
    printf("    INIT_HW: %s\n", ok ? "OK" : "FAIL");
    if (!ok) { CloseHandle(gPsp); return 1; }

    printf("\n[+] Before loading SMU\n");
    PrintSMU();
    PrintStatus();

    printf("\n[+] Step 1: Cycle SMU firmware fwTypes 0-15 via IOCTL_PSP_LOAD_IP_FW_DIRECT\n");
    const char* smcPath = "C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\Firmware\\cyan_skillfish2_smc.bin";
    for (ULONG fwType = 0; fwType <= 15; fwType++) {
        LoadFirmware(fwType, smcPath);
    }
    Sleep(1500);
    PrintSMU();
    PrintStatus();

    printf("\n[+] Step 2: SMU_WAKE GetInfo after fwType sweep\n");
    {
        ULONG resp = 0, status = 0;
        ok = SmuWake(0x01, 0, &resp, &status);
        printf("  GetSmuInfo: IOCTL=%s Resp=0x%08X Status=0x%08X\n", ok ? "OK" : "FAIL", resp, status);
    }
    PrintSMU();
    PrintStatus();

    printf("\n[+] Step 3: Hard SMU_WAKE register writes (0x16A08/0x16A48)\n");
    for (int i = 0; i < 5; i++) {
        WriteReg(0x16A08, 0x01);
        WriteReg(0x16A48, 0x00);
        Sleep(500);
        printf("  Attempt %d: 0x16A08=0x%08X 0x16A48=0x%08X 0x16A68=0x%08X\n",
               i+1, ReadReg(0x16A08), ReadReg(0x16A48), ReadReg(0x16A68));
    }
    PrintSMU();
    PrintStatus();

    printf("\n[+] Step 4: Alternative SMU register block (0x16C00-0x16C20 SMUIO/CLK)\n");
    printf("  CLK=0x%08X SMUIO=0x%08X MP0=0x%08X MP1=0x%08X\n",
           ReadReg(0x16C00), ReadReg(0x16C04), ReadReg(0x16000), ReadReg(0x16004));
    printf("  THM=0x%08X UMC0=0x%08X FUSE=0x%08X HDP=0x%08X\n",
           ReadReg(0x16600), ReadReg(0x14000), ReadReg(0x17400), ReadReg(0x0F20));
    PrintSMU();
    PrintStatus();

    printf("\n=== DONE ===\n");
    CloseHandle(gPsp);
    return 0;
}
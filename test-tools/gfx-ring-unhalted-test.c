#include <windows.h>
#include <stdio.h>

#define PSP_DEVICE      L"\\\\.\\AmdBcPsp"
#define PSP_CTL(fn)     CTL_CODE(FILE_DEVICE_UNKNOWN, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_INIT_HW   PSP_CTL(0x803)
#define IOCTL_PSP_READ_REG  PSP_CTL(0x800)
#define IOCTL_PSP_WRITE_REG PSP_CTL(0x801)

static HANDLE gPsp = INVALID_HANDLE_VALUE;

static ULONG ReadReg(ULONG off) {
    ULONG in[2] = {off, 0}, out[2] = {0}, br = 0;
    DeviceIoControl(gPsp, IOCTL_PSP_READ_REG, in, 4, out, 8, &br, NULL);
    return out[0];
}

static BOOL WriteReg(ULONG off, ULONG val) {
    ULONG in[2] = {off, val}, out[2] = {0}, br = 0;
    return DeviceIoControl(gPsp, IOCTL_PSP_WRITE_REG, in, 8, out, 8, &br, NULL);
}

int main(void) {
    printf("=== GFX Ring Test (ME unhalted) ===\n\n");

    gPsp = CreateFileW(PSP_DEVICE, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gPsp == INVALID_HANDLE_VALUE) {
        printf("FAIL: PSP device not found (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("PSP device opened OK\n");

    printf("\n--- PSP INIT_HW ---\n");
    {
        UCHAR req[32] = {0};
        *(ULONGLONG*)(req+0) = 0xFE800000ULL;
        *(ULONG*)(req+8) = 0x80000;
        ULONG out = 0; DWORD br = 0;
        BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_INIT_HW, req, 12, &out, 4, &br, NULL);
        printf("INIT_HW: %s, VA=0x%08lX\n", ok ? "OK" : "FAIL", out);
        if (!ok) return 1;
    }

    printf("\n--- Step 1: Read current state ---\n");
    printf("GPU_ID        = 0x%08X\n", ReadReg(0x0000));
    printf("ME_CNTL       = 0x%08X\n", ReadReg(0x4A74));
    printf("GRBM_STATUS   = 0x%08X\n", ReadReg(0x3260));
    printf("SCRATCH       = 0x%08X\n", ReadReg(0x32D4));

    printf("\n--- Step 2: GFX Ring0 registers (0xDA60 range) ---\n");
    ULONG base0 = ReadReg(0xDA60);
    ULONG cntl0 = ReadReg(0xDA68);
    ULONG rptr0 = ReadReg(0xDA6C);
    ULONG wptr0 = ReadReg(0xDA78);
    ULONG door0 = ReadReg(0xDA7C);
    printf("BASE_LO(0xDA60)  = 0x%08X\n", base0);
    printf("CNTL(0xDA68)     = 0x%08X\n", cntl0);
    printf("RPTR(0xDA6C)     = 0x%08X\n", rptr0);
    printf("WPTR(0xDA78)     = 0x%08X\n", wptr0);
    printf("DOORBELL(0xDA7C) = 0x%08X\n", door0);

    printf("\n--- Step 3: Unhalt ME if needed ---\n");
    ULONG me = ReadReg(0x4A74);
    if (me != 0 && me != 0xFFFFFFFF) {
        printf("ME is halted (0x%08X), writing 0...\n", me);
        WriteReg(0x4A74, 0);
        printf("ME_CNTL after = 0x%08X\n", ReadReg(0x4A74));
    } else {
        printf("ME already unhalted (0x%08X)\n", me);
    }

    printf("\n--- Step 4: WPTR kick test ---\n");
    wptr0 = ReadReg(0xDA78);
    printf("WPTR before = 0x%08X\n", wptr0);

    /* Try writing WPTR = current + 8 (one PM4 packet) */
    ULONG newWptr = wptr0 + 8;
    printf("Writing WPTR = 0x%08X (wptr+8)...\n", newWptr);
    WriteReg(0xDA78, newWptr);

    ULONG rptrAfter = ReadReg(0xDA6C);
    ULONG wptrAfter = ReadReg(0xDA78);
    printf("RPTR after  = 0x%08X\n", rptrAfter);
    printf("WPTR after  = 0x%08X\n", wptrAfter);
    printf("RPTR changed? %s\n", (rptrAfter != rptr0) ? "YES" : "NO");
    printf("WPTR stuck?  %s\n", (wptrAfter == wptr0) ? "YES (writes don't stick)" : "NO");

    printf("\n--- Step 5: COMPUTE ring (0xDB60 range) ---\n");
    printf("CP_HQD_ACTIVE(0x910C) = 0x%08X\n", ReadReg(0x910C));
    printf("MQD_BASE(0x9104)     = 0x%08X\n", ReadReg(0x9104));
    printf("DISPATCH_INIT(0x80E0)= 0x%08X\n", ReadReg(0x80E0));
    printf("PGM_LO(0x8110)       = 0x%08X\n", ReadReg(0x8110));
    printf("KIQ_BASE(0xE060)     = 0x%08X\n", ReadReg(0xE060));
    printf("KIQ_SIZE(0xE068)     = 0x%08X\n", ReadReg(0xE068));
    printf("KIQ_RPTR(0xE06C)     = 0x%08X\n", ReadReg(0xE06C));
    printf("KIQ_WPTR(0xE078)     = 0x%08X\n", ReadReg(0xE078));

    printf("\n--- Step 6: SDMA ring ---\n");
    printf("SDMA0_CNTL(0xE018)  = 0x%08X\n", ReadReg(0xE018));
    printf("SDMA0_RB_WPTR(0xE010)= 0x%08X\n", ReadReg(0xE010));

    printf("\n--- Step 7: Re-read GRBM ---\n");
    printf("GRBM_STATUS   = 0x%08X\n", ReadReg(0x3260));
    printf("SCRATCH       = 0x%08X\n", ReadReg(0x32D4));

    printf("\n=== DONE ===\n");
    CloseHandle(gPsp);
    return 0;
}

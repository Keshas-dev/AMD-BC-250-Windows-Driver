#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE       0x80000B80
#define IOCTL_AMDBC250_KIQ_BIOS_RING_SUBMIT 0x80000BE0

typedef struct {
    UINT32 Result;
    UINT32 ScratchBefore;
    UINT32 ScratchAfter;
    UINT32 KiqBaseLo;
    UINT32 KiqBaseHi;
    UINT32 KiqRptrBefore;
    UINT32 KiqRptrAfter;
    UINT32 KiqWptrSet;
    UINT32 MeCntlBefore;
    UINT32 MeCntlAfter;
    UINT32 RingDword0;
    UINT32 RingDword1;
    UINT32 RingDword2;
    UINT32 RingDword3;
} KIQ_BIOS_RING_OUT;

static const char *DevicePath = "\\\\.\\AMDBC250DreamV43";

int main(int argc, char *argv[]) {
    printf("=== KIQ BIOS Ring Submit Test ===\n");
    printf("Maps the BIOS-configured KIQ ring, writes PM4 NOP+WRITE_REG,\n");
    printf("and checks if GPU can execute commands through BIOS GCVM mapping.\n\n");

    HANDLE h = CreateFileA(DevicePath, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("GPU driver opened\n");

    DWORD br = 0;

    /* INIT_HARDWARE */
    printf("\n--- INIT_HARDWARE ---\n");
    UCHAR initIn[32] = {0};
    UCHAR initOut[32] = {0};
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 2;
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;
    *(UINT32*)(initIn + 24) = 0x10000000;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
        initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
    printf("INIT_HARDWARE: %s\n", ok ? "OK" : "FAIL");

    /* KIQ BIOS Ring Submit */
    printf("\n--- KIQ BIOS Ring Submit ---\n");
    printf("Using KIQ_BASE from hardware registers\n");

    KIQ_BIOS_RING_OUT out = {0};
    ok = DeviceIoControl(h, IOCTL_AMDBC250_KIQ_BIOS_RING_SUBMIT,
        NULL, 0, &out, sizeof(out), &br, NULL);

    if (!ok) {
        printf("KIQ_BIOS_RING_SUBMIT failed (err=%lu)\n", GetLastError());
    } else {
        printf("  Result                      = %u", out.Result);
        if (out.Result == 2) printf(" (SUCCESS! PM4 executed!)\n");
        else if (out.Result == 1) printf(" (RPTR advanced)\n");
        else if (out.Result == 0) printf(" (no progress)\n");
        else printf(" (error 0x%08X)\n", out.Result);

        printf("  KIQ_BASE                    = 0x%08X%08X\n", out.KiqBaseHi, out.KiqBaseLo);
        printf("  SCRATCH before              = 0x%08X\n", out.ScratchBefore);
        printf("  SCRATCH after               = 0x%08X\n", out.ScratchAfter);
        printf("  KIQ_RPTR before             = 0x%08X\n", out.KiqRptrBefore);
        printf("  KIQ_RPTR after              = 0x%08X\n", out.KiqRptrAfter);
        printf("  KIQ_WPTR set                = 0x%08X\n", out.KiqWptrSet);
        printf("  ME_CNTL before              = 0x%08X\n", out.MeCntlBefore);
        printf("  ME_CNTL after               = 0x%08X\n", out.MeCntlAfter);
        printf("  Ring[0..3] after            = 0x%08X 0x%08X 0x%08X 0x%08X\n",
            out.RingDword0, out.RingDword1, out.RingDword2, out.RingDword3);
    }

    CloseHandle(h);
    printf("\nDone.\n");
    return 0;
}

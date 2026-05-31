/*
 * BC-250 Rendering Test - Fill screen with color via SDMA
 * Allocates framebuffer, fills with color, presents to display
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_AMDBC250_ALLOC_DMA_BUFFER  0x80000930
#define IOCTL_AMDBC250_FREE_DMA_BUFFER   0x80000934
#define IOCTL_AMDBC250_SDMA_FILL         0x80000944
#define IOCTL_AMDBC250_FLIP_DISPLAY      0x800008C4
#define IOCTL_AMDBC250_SET_DISPLAY_MODE  0x800008C0
#define IOCTL_AMDBC250_SUBMIT_COMMANDS   0x80000880
#define IOCTL_AMDBC250_WAIT_FENCE        0x80000884
#define IOCTL_AMDBC250_GET_DISPLAY_INFO  0x800008C8

static HANDLE OpenKmd(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static BOOL Ioctl(HANDLE h, DWORD code, void* in, DWORD inSz, void* out, DWORD outSz) {
    DWORD ret = 0;
    return DeviceIoControl(h, code, in, inSz, out, outSz, &ret, NULL);
}

int main(void) {
    printf("========================================\n");
    printf("  BC-250 Rendering Test\n");
    printf("========================================\n\n");

    HANDLE kmd = OpenKmd();
    if (kmd == INVALID_HANDLE_VALUE) {
        printf("[FAIL] Cannot open KMD device\n");
        return 1;
    }
    printf("[OK] KMD device opened\n");

    /* Step 1: Set display mode to 800x600 */
    printf("\n--- Setting display mode 800x600 ---\n");
    {
        ULONG modeData[6] = {0};
        modeData[0] = 800;    /* Width */
        modeData[1] = 600;    /* Height */
        modeData[2] = 60;     /* RefreshRate */
        modeData[3] = 32;     /* BitsPerPixel */
        modeData[4] = 0;      /* Format */
        modeData[5] = 1;      /* Active */
        if (Ioctl(kmd, IOCTL_AMDBC250_SET_DISPLAY_MODE, modeData, sizeof(modeData), NULL, 0)) {
            printf("[OK] Display mode set to 800x600x32\n");
        } else {
            printf("[WARN] SetDisplayMode failed (err=%lu)\n", GetLastError());
        }
    }

    /* Step 2: Allocate framebuffer (800x600x4 = 1,920,000 bytes) */
    printf("\n--- Allocating framebuffer ---\n");
    uint64_t fbPa = 0;
    PVOID fbVa = NULL;
    {
        ULONG allocIn[4] = {0};
        allocIn[0] = 1920000;  /* Size */
        ULONG64 allocOut[2] = {0};
        DWORD ret = 0;
        if (DeviceIoControl(kmd, IOCTL_AMDBC250_ALLOC_DMA_BUFFER,
                            allocIn, sizeof(allocIn), allocOut, sizeof(allocOut), &ret, NULL)) {
            fbPa = allocOut[0];
            fbVa = (PVOID)(UINT_PTR)allocOut[1];
            printf("[OK] Framebuffer: PA=0x%llX VA=%p (%d bytes)\n", fbPa, fbVa, 1920000);
        } else {
            printf("[FAIL] Alloc DMA buffer failed (err=%lu)\n", GetLastError());
            CloseHandle(kmd);
            return 1;
        }
    }

    if (!fbVa) {
        printf("[FAIL] No framebuffer memory\n");
        CloseHandle(kmd);
        return 1;
    }

    /* Step 3: Fill framebuffer with blue color (0xFF0000) using CPU */
    printf("\n--- Filling framebuffer with blue ---\n");
    {
        ULONG* pixels = (ULONG*)fbVa;
        for (int i = 0; i < 800 * 600; i++) {
            pixels[i] = 0x00FF0000;  /* BGRA: Blue */
        }
        printf("[OK] Framebuffer filled with blue\n");
    }

    /* Step 4: Present framebuffer to display */
    printf("\n--- Presenting to display ---\n");
    {
        ULONG flipData[7] = {0};
        flipData[0] = (ULONG)(fbPa & 0xFFFFFFFF);    /* Surface PA low */
        flipData[1] = (ULONG)(fbPa >> 32);             /* Surface PA high */
        flipData[2] = 800;                              /* Width */
        flipData[3] = 600;                              /* Height */
        flipData[4] = 800 * 4;                          /* Pitch (bytes) */
        flipData[5] = 22;                               /* D3DDDIFMT_A8R8G8B8 */
        flipData[6] = 0;                                /* VSync off */
        if (Ioctl(kmd, IOCTL_AMDBC250_FLIP_DISPLAY, flipData, sizeof(flipData), NULL, 0)) {
            printf("[OK] Flip submitted - check monitor!\n");
        } else {
            printf("[FAIL] Flip failed (err=%lu)\n", GetLastError());
        }
    }

    /* Step 5: Wait a bit then cleanup */
    printf("\n--- Waiting 3 seconds ---\n");
    Sleep(3000);

    /* Step 6: Free framebuffer */
    printf("\n--- Cleanup ---\n");
    {
        ULONG64 freeIn[1] = {(ULONG64)(UINT_PTR)fbVa};  /* Pass VA, NOT PA! */
        DWORD ret = 0;
        DeviceIoControl(kmd, IOCTL_AMDBC250_FREE_DMA_BUFFER, freeIn, sizeof(freeIn), NULL, 0, &ret, NULL);
        printf("[OK] Framebuffer freed\n");
    }

    CloseHandle(kmd);
    printf("\n=== Rendering test complete ===\n");
    return 0;
}

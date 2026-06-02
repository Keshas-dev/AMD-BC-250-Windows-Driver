#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <d3dkmthk.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\safe-test.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== Safe Test Start ===\n");
    fflush(g);

    Log("Step 1: D3DKMTEnumAdapters\n"); fflush(g);
    {
        D3DKMT_ENUMADAPTERS ea = {0};
        NTSTATUS st = D3DKMTEnumAdapters(&ea);
        Log("  Result: 0x%08X  NumAdapters=%u\n", st, ea.NumAdapters); fflush(g);
    }

    Log("Step 2: D3DKMTOpenAdapterFromHdc\n"); fflush(g);
    {
        HDC hdc = GetDC(NULL);
        D3DKMT_OPENADAPTERFROMHDC oah = {0};
        oah.hDc = hdc;
        NTSTATUS st = D3DKMTOpenAdapterFromHdc(&oah);
        Log("  Result: 0x%08X  hAdapter=0x%08X\n", st, oah.hAdapter); fflush(g);
        ReleaseDC(NULL, hdc);
    }

    {
        HANDLE h;
        UCHAR inBuf[64] = {0}, outBuf[64] = {0};
        DWORD br = 0;

        Log("Step 3: IOCTL GET_CAPS\n"); fflush(g);
        h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
            GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            Log("  KMD device NOT FOUND error=%lu\n", GetLastError()); fflush(g);
            goto done;
        }
        {
            BOOL ok = DeviceIoControl(h, 0x80000800, inBuf, sizeof(inBuf),
                outBuf, sizeof(outBuf), &br, NULL);
            Log("  GET_CAPS: %s  Version=%u  CUs=%u  GPUCLK=%u MHz\n",
                ok ? "OK" : "FAIL",
                *(UINT32*)(outBuf+0), *(UINT32*)(outBuf+16), *(UINT32*)(outBuf+8));
            fflush(g);
        }

        Log("Step 4: IOCTL GET_VRAM_INFO\n"); fflush(g);
        {
            BOOL ok = DeviceIoControl(h, 0x80000804, inBuf, sizeof(inBuf),
                outBuf, sizeof(outBuf), &br, NULL);
            Log("  GET_VRAM_INFO: %s  Total=%llu MB\n",
                ok ? "OK" : "FAIL", *(UINT64*)(outBuf+0) / (1024*1024));
            fflush(g);
        }

        Log("Step 5: IOCTL INIT_HARDWARE (Flags=1 NBIO_MAP)\n"); fflush(g);
        {
            UCHAR initIn[32] = {0};
            UCHAR initOut[32] = {0};
            *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
            *(UINT32*)(initIn + 8)  = 0x00080000;
            *(UINT32*)(initIn + 12) = 1;
            *(UINT64*)(initIn + 16) = 0xC0000000ULL;
            *(UINT32*)(initIn + 24) = 0x10000000;
            BOOL ok = DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn),
                initOut, sizeof(initOut), &br, NULL);
            Log("  INIT_HARDWARE: %s\n", ok ? "OK" : "FAIL"); fflush(g);
        }

        Log("Step 6: READ_REG GPU_ID [0x0000]\n"); fflush(g);
        {
            UINT32 ra[2] = {0x0000, 0xDEADBEEF};
            BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
            Log("  READ_REG[0x0000]: %s  GPU_ID=0x%08X\n",
                ok ? "OK" : "FAIL", ra[1]); fflush(g);
        }

        Log("Step 7: READ_REG HDP [0x05A0,0x05A4,0x05C8,0x05CC,0x05D0,0x05D4,0x05D8,0x05DC]\n");
        fflush(g);
        {
            UINT32 hdpOffs[] = {0x05A0,0x05A4,0x05C8,0x05CC,0x05D0,0x05D4,0x05D8,0x05DC};
            int i;
            for (i = 0; i < 8; i++) {
                UINT32 ra[2] = {hdpOffs[i], 0xDEADBEEF};
                BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
                if (ok && ra[1] != 0xFFFFFFFF)
                    Log("  HDP[0x%04X] = 0x%08X\n", hdpOffs[i], ra[1]);
            }
            fflush(g);
        }

        Log("Step 8: READ_REG GC [0x3000,0x3004,0x3008]\n"); fflush(g);
        {
            UINT32 gcOffs[] = {0x3000,0x3004,0x3008};
            int i;
            for (i = 0; i < 3; i++) {
                UINT32 ra[2] = {gcOffs[i], 0xDEADBEEF};
                BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
                if (ok && ra[1] != 0xFFFFFFFF)
                    Log("  GC[0x%04X] = 0x%08X\n", gcOffs[i], ra[1]);
            }
            fflush(g);
        }

        Log("Step 9: MMHUB scan (BAR5+0x5000, 128 regs)\n"); fflush(g);
        {
            int count = 0;
            UINT32 i;
            for (i = 0; i < 128; i++) {
                UINT32 ra[2] = {0x5000 + i*4, 0xDEADBEEF};
                BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
                if (ok && ra[1] != 0xFFFFFFFF && ra[1] != 0x00000000) {
                    Log("  MMHUB[0x%04X] = 0x%08X\n", 0x5000 + i*4, ra[1]);
                    count++;
                }
            }
            Log("  MMHUB readable: %d\n", count); fflush(g);
        }

        Log("Step 10: DF scan (BAR5+0x1A000, 64 regs)\n"); fflush(g);
        {
            int count = 0;
            UINT32 i;
            for (i = 0; i < 64; i++) {
                UINT32 ra[2] = {0x1A000 + i*4, 0xDEADBEEF};
                BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
                if (ok && ra[1] != 0xFFFFFFFF && ra[1] != 0x00000000) {
                    Log("  DF[0x%05X] = 0x%08X\n", 0x1A000 + i*4, ra[1]);
                    count++;
                }
            }
            Log("  DF readable: %d\n", count); fflush(g);
        }

        Log("Step 11: GRBM_STATUS attempt [0x2004]\n"); fflush(g);
        {
            UINT32 ra[2] = {0x2004, 0xDEADBEEF};
            BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
            Log("  GRBM_STATUS[0x2004]: %s  Value=0x%08X\n",
                ok ? "OK" : "FAIL", ra[1]); fflush(g);
        }

        Log("Step 12: Scratch regs [0x2074,0x2078,0x207C]\n"); fflush(g);
        {
            UINT32 scOffs[] = {0x2074,0x2078,0x207C};
            int i;
            for (i = 0; i < 3; i++) {
                UINT32 ra[2] = {scOffs[i], 0xDEADBEEF};
                BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
                Log("  SCRATCH[0x%04X]: %s  0x%08X\n", scOffs[i],
                    ok ? "OK" : "FAIL", ra[1]);
            }
            fflush(g);
        }

        Log("Step 13: RSMU scan (BAR5+0xA000, 32 regs)\n"); fflush(g);
        {
            int count = 0;
            UINT32 i;
            for (i = 0; i < 32; i++) {
                UINT32 ra[2] = {0xA000 + i*4, 0xDEADBEEF};
                BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
                if (ok && ra[1] != 0xFFFFFFFF && ra[1] != 0x00000000) {
                    Log("  RSMU[0x%04X] = 0x%08X\n", 0xA000 + i*4, ra[1]);
                    count++;
                }
            }
            Log("  RSMU readable: %d\n", count); fflush(g);
        }

        Log("Step 14: GET_HW_STATUS\n"); fflush(g);
        {
            UCHAR hwBuf[64] = {0};
            BOOL ok = DeviceIoControl(h, 0x80000B90, inBuf, 0, hwBuf, sizeof(hwBuf), &br, NULL);
            Log("  GET_HW_STATUS: %s  MMIO=%u Rings=%u Fence=%u\n",
                ok ? "OK" : "FAIL",
                *(UINT32*)(hwBuf+0), *(UINT32*)(hwBuf+4), *(UINT32*)(hwBuf+8));
            fflush(g);
        }

        CloseHandle(h);
    }

done:
    Log("=== Safe Test Complete ===\n"); fflush(g);
    if (g) fclose(g);
    printf("Done. Check output\\safe-test.log\n");
    return 0;
}

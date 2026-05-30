/*
 * BC-250 IOCTL Test Tool
 * Tests direct KMD ↔ UMD communication via IOCTL
 */

#include <windows.h>
#include <windef.h>
#include <stdio.h>

/* IOCTL definitions (must match KMD) */
#define DEVICE_PATH     L"\\\\.\\AMDBC250DreamV43"
#define IOCTL_GET_CAPS          0x80000800
#define IOCTL_GET_VRAM_INFO     0x80000804
#define IOCTL_GET_TEMP_INFO     0x80000808
#define IOCTL_ALLOC_VIDMEM      0x80000840
#define IOCTL_FREE_VIDMEM       0x80000844
#define IOCTL_MAP_VIDMEM        0x80000848
#define IOCTL_SUBMIT_COMMANDS   0x80000880
#define IOCTL_WAIT_FENCE        0x80000884
#define IOCTL_SIGNAL_FENCE      0x80000888
#define IOCTL_SET_DISPLAY_MODE  0x800008C0
#define IOCTL_FLIP_DISPLAY      0x800008C4
#define IOCTL_GET_DISPLAY_INFO  0x800008C8
#define IOCTL_SET_POWER_STATE   0x80000900
#define IOCTL_GET_POWER_TELEM   0x80000904
#define IOCTL_SDMA_COPY         0x80000940
#define IOCTL_SDMA_FILL         0x80000944
#define IOCTL_TDR_RESET         0x80000950
#define IOCTL_READ_EDID         0x80000960
#define IOCTL_GET_CHILD_REL     0x80000964
#define IOCTL_SHADER_COMPILE    0x80000970

static HANDLE OpenKmd(void)
{
    HANDLE h = CreateFileW(DEVICE_PATH, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[FAIL] Cannot open %ls (error %lu)\n", DEVICE_PATH, GetLastError());
        printf("       Make sure driver is installed and testsigning is on.\n");
    }
    return h;
}

static BOOL DoIoctl(HANDLE h, DWORD code, PVOID in, DWORD inSz, PVOID out, DWORD outSz)
{
    DWORD ret = 0;
    return DeviceIoControl(h, code, in, inSz, out, outSz, &ret, NULL);
}

static int testCount = 0;
static int passCount = 0;

void TestResult(const char* name, BOOL ok)
{
    testCount++;
    if (ok) passCount++;
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
}

int main(void)
{
    printf("========================================\n");
    printf("  BC-250 IOCTL Communication Test\n");
    printf("========================================\n\n");

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) return 1;

    BOOL ok;
    DWORD bytes;

    /* Test 1: Get Capabilities */
    {
        ULONG caps[7] = {0};
        ok = DoIoctl(h, IOCTL_GET_CAPS, NULL, 0, caps, sizeof(caps));
        TestResult("GetCaps", ok && caps[0] > 0);
        if (ok) {
            printf("  Version: %lu.%lu.%lu\n", caps[0]/100, (caps[0]/10)%10, caps[0]%10);
            printf("  CUs: %lu, SPs: %lu, RT: %lu\n", caps[4], caps[5], caps[6]);
            printf("  SCLK: %lu MHz, MCLK: %lu MHz\n", caps[2], caps[3]);
        }
    }

    /* Test 2: Get VRAM Info */
    {
        ULONG64 vram[3] = {0};
        ULONG segCount = 0;
        ok = DoIoctl(h, IOCTL_GET_VRAM_INFO, NULL, 0, vram, sizeof(vram) + sizeof(segCount));
        TestResult("GetVramInfo", ok && vram[0] > 0);
        if (ok) {
            printf("  Total: %llu MB\n", vram[0] / (1024*1024));
            printf("  Visible: %llu MB\n", vram[1] / (1024*1024));
            printf("  Used: %llu MB\n", vram[2] / (1024*1024));
        }
    }

    /* Test 3: Get Temperature */
    {
        ULONG temp[4] = {0};
        BOOLEAN throttle = FALSE;
        ok = DoIoctl(h, IOCTL_GET_TEMP_INFO, NULL, 0, temp, sizeof(temp) + sizeof(throttle));
        TestResult("GetTempInfo", ok);
        if (ok) {
            printf("  Edge: %ld C, Junction: %ld C\n", (LONG)temp[0], (LONG)temp[1]);
            printf("  Fan: %lu%%, Throttle: %s\n", temp[3], throttle ? "YES" : "no");
        }
    }

    /* Test 4: Get Display Info */
    {
        ULONG disp[7] = {0};
        ok = DoIoctl(h, IOCTL_GET_DISPLAY_INFO, NULL, 0, disp, sizeof(disp));
        TestResult("GetDisplayInfo", ok);
        if (ok) {
            printf("  Current: %lux%lu @ %luHz\n", disp[0], disp[1], disp[2]);
            printf("  Max: %lux%lu, Pipes: %lu\n", disp[3], disp[4], disp[6]);
        }
    }

    /* Test 5: Set Display Mode */
    {
        ULONG mode[4] = {1920, 1080, 60, 32};
        ok = DoIoctl(h, IOCTL_SET_DISPLAY_MODE, mode, sizeof(mode), NULL, 0);
        TestResult("SetDisplayMode(1920x1080@60)", ok);
    }

    /* Test 6: Allocate Video Memory */
    {
        ULONG allocIn[4] = {4096, 0, 0x3, 0}; /* 4KB, READ|WRITE, VRAM */
        ULONG64 allocOut[3] = {0};
        ok = DoIoctl(h, IOCTL_ALLOC_VIDMEM, allocIn, sizeof(allocIn), allocOut, sizeof(allocOut));
        TestResult("AllocVidMem(1MB)", ok && allocOut[0] > 0);
        if (ok) {
            printf("  GPU VA: 0x%llX\n", allocOut[0]);
            printf("  Phys:  0x%llX\n", allocOut[1]);

            /* Test 7: Free Video Memory */
            ULONG64 freeIn[1] = {allocOut[2]};
            BOOL freed = DoIoctl(h, IOCTL_FREE_VIDMEM, freeIn, sizeof(freeIn), NULL, 0);
            TestResult("FreeVidMem", freed);
        }
    }

    /* Test 8: SDMA Fill */
    {
        ULONG fillIn[4] = {0, 0x1000, 0x1000, 0xDEADBEEF}; /* PA, Size, Fill */
        ok = DoIoctl(h, IOCTL_SDMA_FILL, fillIn, sizeof(fillIn), NULL, 0);
        TestResult("SdmaFill(stub)", ok);
    }

    /* Test 9: Signal Fence */
    {
        ULONG fenceIn[1] = {42};
        ok = DoIoctl(h, IOCTL_SIGNAL_FENCE, fenceIn, sizeof(fenceIn), NULL, 0);
        TestResult("SignalFence(42)", ok);
    }

    /* Test 10: Wait Fence */
    {
        ULONG waitIn[2] = {42, 1000}; /* fence=42, timeout=1s */
        ok = DoIoctl(h, IOCTL_WAIT_FENCE, waitIn, sizeof(waitIn), NULL, 0);
        TestResult("WaitFence(42)", ok);
    }

    /* Test 11: TDR Reset */
    {
        ok = DoIoctl(h, IOCTL_TDR_RESET, NULL, 0, NULL, 0);
        TestResult("TdrReset", ok);
    }

    /* Test 12: Read EDID */
    {
        ULONG childIn[1] = {0};
        UCHAR edid[256] = {0};
        ok = DoIoctl(h, IOCTL_READ_EDID, childIn, sizeof(childIn), edid, sizeof(edid));
        TestResult("ReadEdid(child0)", ok && edid[0] == 0x00 && edid[1] == 0xFF);
        if (ok) {
            printf("  Header: %02X %02X %02X %02X\n", edid[0], edid[1], edid[2], edid[3]);
        }
    }

    /* Test 13: Get Child Relations */
    {
        ULONG childRel[3] = {0};
        ok = DoIoctl(h, IOCTL_GET_CHILD_REL, NULL, 0, childRel, sizeof(childRel));
        TestResult("GetChildRelations", ok);
        if (ok) {
            printf("  Children: %lu, Connected: 0x%lX\n", childRel[0], childRel[1]);
        }
    }

    /* Test 14: Shader Compile (stub) */
    {
        ULONG shaderIn[2] = {1, 256}; /* PS, 256 bytes */
        ULONG shaderOut[2] = {0};
        ok = DoIoctl(h, IOCTL_SHADER_COMPILE, shaderIn, sizeof(shaderIn), shaderOut, sizeof(shaderOut));
        TestResult("ShaderCompile(stub)", ok);
    }

    /* Test 15: Get Power Telemetry */
    {
        ULONG telem[9] = {0};
        ok = DoIoctl(h, IOCTL_GET_POWER_TELEM, NULL, 0, telem, sizeof(telem));
        TestResult("GetPowerTelemetry", ok);
        if (ok) {
            printf("  SCLK: %lu MHz, MCLK: %lu MHz\n", telem[2], telem[3]);
            printf("  Fan: %lu%%, Temp: %ld C\n", telem[4], (LONG)telem[5]);
        }
    }

    CloseHandle(h);

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", passCount, testCount);
    printf("========================================\n");

    return (passCount == testCount) ? 0 : 1;
}

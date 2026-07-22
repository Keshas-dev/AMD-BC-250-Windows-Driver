/*
 * ioctl-robustness-test.c
 *
 * IOCTL robustness / fuzz-lite test for AMDBC250 driver.
 *
 * WARNING: this test intentionally sends invalid parameters and may trigger
 * bugchecks if the driver does not validate inputs. Run only on test hardware
 * with test-signing enabled and be prepared to hard-reset.
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <intrin.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static void fatal(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("FATAL: %s\n", buf);
    if (g_hDev != INVALID_HANDLE_VALUE) CloseHandle(g_hDev);
    ExitProcess(1);
}

static BOOL safe_init(void)
{
    g_hDev = CreateFileW(AMDBC250_DEVICE_PATH,
                         GENERIC_READ | GENERIC_WRITE,
                         0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE)
        fatal("Cannot open device (gle=%lu)", GetLastError());

    AMDBC250_IOCTL_INIT_HARDWARE ih = {0};
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize        = 0x80000;
    ih.Flags           = AMDBC250_INIT_FLAG_NBIO_MAP;
    DWORD br = 0;
    if (!DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE,
                         &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL))
        fatal("INIT_HARDWARE failed (gle=%lu)", GetLastError());

    printf("INIT_HARDWARE OK\n");
    return TRUE;
}

static volatile LONG g_crashes = 0;
static volatile LONG g_pass    = 0;
static volatile LONG g_fail    = 0;

static BOOL run_case(const char *name, DWORD ioctl,
                     const void *pIn, ULONG inSize,
                     void *pOut, ULONG outSize)
{
    DWORD ret = 0;
    __try {
        BOOL ok = DeviceIoControl(g_hDev, ioctl,
                                  (PVOID)(ULONG_PTR)pIn, inSize,
                                  pOut, outSize, &ret, NULL);
        if (ok) {
            InterlockedIncrement(&g_pass);
            printf("  PASS: %s (ret=%lu)\n", name, ret);
        } else {
            DWORD gle = GetLastError();
            InterlockedIncrement(&g_fail);
            printf("  FAIL: %s gle=%lu\n", name, gle);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&g_crashes);
        printf("  CRASH: %s exception=0x%08X\n", name, GetExceptionCode());
    }
    return TRUE;
}

static void* alloc_buf(ULONG bytes)
{
    if (bytes == 0) return NULL;
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
}

static void free_buf(void *p)
{
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

int main(void)
{
    if (!safe_init()) return 1;

    printf("\n=== IOCTL Robustness Test ===\n");

    /* ------------------------------------------------------------------ */
    /* INIT_HARDWARE variations                                           */
    /* ------------------------------------------------------------------ */
    printf("\n--- INIT_HARDWARE ---\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE bad = {0};

        run_case("INIT_HARDWARE: MmioPhysicalBase=0/size=0",
                 IOCTL_AMDBC250_INIT_HARDWARE, &bad, sizeof(bad), &bad, sizeof(bad));

        bad.MmioPhysicalBase = 0xFE800000ULL;
        bad.MmioSize        = 0x80000;
        bad.Flags           = 0xFFFFFFFF;
        run_case("INIT_HARDWARE: Flags=0xFFFFFFFF",
                 IOCTL_AMDBC250_INIT_HARDWARE, &bad, sizeof(bad), &bad, sizeof(bad));
    }

    /* ------------------------------------------------------------------ */
    /* READ_REG / WRITE_REG boundary tests                               */
    /* ------------------------------------------------------------------ */
    printf("\n--- READ_REG ---\n");
    {
        AMDBC250_IOCTL_REG_ACCESS ra = {0};
        run_case("READ_REG: offset=0",
                 IOCTL_AMDBC250_READ_REG, &ra, sizeof(ra), &ra, sizeof(ra));

        ra.RegisterOffset = 0xFFFFFFFF;
        ra.Value = 0;
        run_case("READ_REG: offset=0xFFFFFFFF",
                 IOCTL_AMDBC250_READ_REG, &ra, sizeof(ra), &ra, sizeof(ra));

        ra.RegisterOffset = 0xFE800000ULL;
        run_case("READ_REG: offset=0xFE800000 (BAR5 base, no map)",
                 IOCTL_AMDBC250_READ_REG, &ra, sizeof(ra), &ra, sizeof(ra));
    }

    printf("\n--- WRITE_REG ---\n");
    {
        AMDBC250_IOCTL_REG_ACCESS wa = {0};
        run_case("WRITE_REG: offset=0",
                 IOCTL_AMDBC250_WRITE_REG, &wa, sizeof(wa), &wa, sizeof(wa));

        wa.RegisterOffset = 0xFFFFFFFF;
        wa.Value = 0xFFFFFFFF;
        run_case("WRITE_REG: offset=0xFFFFFFFF",
                 IOCTL_AMDBC250_WRITE_REG, &wa, sizeof(wa), &wa, sizeof(wa));

        wa.RegisterOffset = 0x34D0; /* GRBM_GFX_INDEX — sensitive */
        wa.Value = 0xE0000000;
        run_case("WRITE_REG: GRBM_GFX_INDEX broadcast",
                 IOCTL_AMDBC250_WRITE_REG, &wa, sizeof(wa), &wa, sizeof(wa));

        wa.RegisterOffset = 0x9C1C; /* CC_ARRAY_CONFIG */
        wa.Value = 0xFFFFFFFF;
        run_case("WRITE_REG: CC_ARRAY_CONFIG 0xFFFFFFFF",
                 IOCTL_AMDBC250_WRITE_REG, &wa, sizeof(wa), &wa, sizeof(wa));

        /* restore safe broadcast value */
        wa.Value = 0xE0000000;
        run_case("WRITE_REG: GRBM_GFX_INDEX restore",
                 IOCTL_AMDBC250_WRITE_REG, &wa, sizeof(wa), &wa, sizeof(wa));
    }

    /* ------------------------------------------------------------------ */
    /* SEND_PM4                                                          */
    /* ------------------------------------------------------------------ */
    printf("\n--- SEND_PM4 ---\n");
    {
        AMDBC250_IOCTL_SEND_PM4 pm4 = {0};
        run_case("SEND_PM4: zeroed CommandCount",
                 IOCTL_AMDBC250_SEND_PM4, &pm4, sizeof(pm4), &pm4, sizeof(pm4));

        pm4.CommandCount = 64;
        memset(pm4.Commands, 0xFF, sizeof(pm4.Commands));
        run_case("SEND_PM4: 64 garbage DWORDs",
                 IOCTL_AMDBC250_SEND_PM4, &pm4, sizeof(pm4), &pm4, sizeof(pm4));
    }

    /* ------------------------------------------------------------------ */
    /* PSP LOAD_IP_FW                                                    */
    /* ------------------------------------------------------------------ */
    printf("\n--- PSP_LOAD_IP_FW ---\n");
    {
        AMDBC250_IOCTL_PSP_LOAD_IP_FW fw = {0};
        run_case("PSP_LOAD_IP_FW: fwType=0xFF FwSize=0",
                 IOCTL_AMDBC250_PSP_LOAD_IP_FW, &fw, sizeof(fw), &fw, sizeof(fw));

        fw.FwType  = 0xFF;
        fw.FwSize = 0xFFFFFFFF;
        run_case("PSP_LOAD_IP_FW: fwType=0xFF FwSize=0xFFFFFFFF",
                 IOCTL_AMDBC250_PSP_LOAD_IP_FW, &fw, sizeof(fw), &fw, sizeof(fw));
    }

    /* ------------------------------------------------------------------ */
    /* UNLOCK_40CU / GET_CU_STATUS                                        */
    /* ------------------------------------------------------------------ */
    printf("\n--- UNLOCK_40CU ---\n");
    {
        AMDBC250_IOCTL_UNLOCK_40CU ul = {0};
        run_case("UNLOCK_40CU: enable=0",
                 IOCTL_AMDBC250_UNLOCK_40CU, &ul, sizeof(ul), &ul, sizeof(ul));

        ul.Enable = 0xFFFFFFFF;
        run_case("UNLOCK_40CU: enable=0xFFFFFFFF",
                 IOCTL_AMDBC250_UNLOCK_40CU, &ul, sizeof(ul), &ul, sizeof(ul));
    }

    printf("\n--- GET_CU_STATUS ---\n");
    {
        AMDBC250_IOCTL_GET_CU_STATUS cs = {0};
        run_case("GET_CU_STATUS: default",
                 IOCTL_AMDBC250_GET_CU_STATUS, &cs, sizeof(cs), &cs, sizeof(cs));
    }

    /* ------------------------------------------------------------------ */
    /* PSP_SMU_MSG                                                       */
    /* ------------------------------------------------------------------ */
    printf("\n--- PSP_SMU_MSG ---\n");
    {
        AMDBC250_IOCTL_PSP_SMU_MSG sm = {0};
        run_case("PSP_SMU_MSG: msg=0/arg=0",
                 IOCTL_AMDBC250_PSP_SMU_MSG, &sm, sizeof(sm), &sm, sizeof(sm));

        sm.Message = 0xFFFFFFFF;
        sm.Argument = 0xFFFFFFFF;
        run_case("PSP_SMU_MSG: msg=0xFFFFFFFF arg=0xFFFFFFFF",
                 IOCTL_AMDBC250_PSP_SMU_MSG, &sm, sizeof(sm), &sm, sizeof(sm));
    }

    /* ------------------------------------------------------------------ */
    /* READ_PCI_CONFIG / WRITE_PCI_CONFIG                                 */
    /* ------------------------------------------------------------------ */
    printf("\n--- READ_PCI_CONFIG ---\n");
    {
        AMDBC250_IOCTL_READ_PCI_CONFIG pc = {0};
        run_case("READ_PCI_CONFIG: bus=0/dev=0/func=0",
                 IOCTL_AMDBC250_READ_PCI_CONFIG, &pc, sizeof(pc), &pc, sizeof(pc));

        pc.Bus = 0; pc.Device = 0; pc.Function = 0;
        run_case("READ_PCI_CONFIG: ConfigData[255]=0xFF",
                 IOCTL_AMDBC250_READ_PCI_CONFIG, &pc, sizeof(pc), &pc, sizeof(pc));
    }

    printf("\n--- WRITE_PCI_CONFIG ---\n");
    {
        AMDBC250_IOCTL_WRITE_PCI_CONFIG wc = {0};
        run_case("WRITE_PCI_CONFIG: bus=0/dev=0/func=0 offset=0",
                 IOCTL_AMDBC250_WRITE_PCI_CONFIG, &wc, sizeof(wc), &wc, sizeof(wc));

        wc.Bus = 0; wc.Device = 0; wc.Function = 0;
        wc.Offset = 0xFF; wc.Value = 0xFFFFFFFF;
        run_case("WRITE_PCI_CONFIG: offset=0xFF value=0xFFFFFFFF",
                 IOCTL_AMDBC250_WRITE_PCI_CONFIG, &wc, sizeof(wc), &wc, sizeof(wc));
    }

    /* ------------------------------------------------------------------ */
    /* MMIO_TEST                                                         */
    /* ------------------------------------------------------------------ */
    printf("\n--- MMIO_TEST ---\n");
    {
        AMDBC250_IOCTL_MMIO_TEST mt = {0};
        run_case("MMIO_TEST: pa=0/size=0",
                 IOCTL_AMDBC250_MMIO_TEST, &mt, sizeof(mt), &mt, sizeof(mt));

        mt.PhysicalAddress = 0xFFFFFFFFFFFFFFFFULL;
        mt.Size = 0xFFFFFFFF;
        mt.ValueWrite = 0xFFFFFFFF;
        run_case("MMIO_TEST: pa=0xFFFFFFFFFFFFFFFF size=0xFFFFFFFF",
                 IOCTL_AMDBC250_MMIO_TEST, &mt, sizeof(mt), &mt, sizeof(mt));
    }

    /* ------------------------------------------------------------------ */
    /* EXECUTE_RING_PM4                                                   */
    /* ------------------------------------------------------------------ */
    printf("\n--- EXECUTE_RING_PM4 ---\n");
    {
        AMDBC250_IOCTL_EXECUTE_RING_PM4 ep = {0};
        run_case("EXECUTE_RING_PM4: zeroed",
                 IOCTL_AMDBC250_EXECUTE_RING_PM4, &ep, sizeof(ep), &ep, sizeof(ep));

        ep.CommandCount = 64;
        memset(ep.Commands, 0xFF, sizeof(ep.Commands));
        run_case("EXECUTE_RING_PM4: 64 garbage DWORDs",
                 IOCTL_AMDBC250_EXECUTE_RING_PM4, &ep, sizeof(ep), &ep, sizeof(ep));
    }

    /* ------------------------------------------------------------------ */
    /* KIQ_NOP_TEST / KIQ_BIOS_RING_SUBMIT                                */
    /* ------------------------------------------------------------------ */
    printf("\n--- KIQ_NOP_TEST ---\n");
    {
        AMDBC250_IOCTL_KIQ_NOP_TEST kt = {0};
        run_case("KIQ_NOP_TEST: zeroed",
                 IOCTL_AMDBC250_KIQ_NOP_TEST, &kt, sizeof(kt), &kt, sizeof(kt));

        kt.KiqWptrSet = 0xFFFFFFFF;
        run_case("KIQ_NOP_TEST: KiqWptrSet=0xFFFFFFFF",
                 IOCTL_AMDBC250_KIQ_NOP_TEST, &kt, sizeof(kt), &kt, sizeof(kt));
    }

    printf("\n--- KIQ_BIOS_RING_SUBMIT ---\n");
    {
        AMDBC250_IOCTL_KIQ_BIOS_RING_SUBMIT kb = {0};
        run_case("KIQ_BIOS_RING_SUBMIT: default",
                 IOCTL_AMDBC250_KIQ_BIOS_RING_SUBMIT, &kb, sizeof(kb), &kb, sizeof(kb));
    }

    /* ------------------------------------------------------------------ */
    /* GCVM_PT_SETUP                                                     */
    /* ------------------------------------------------------------------ */
    printf("\n--- GCVM_PT_SETUP ---\n");
    {
        AMDBC250_IOCTL_GCVM_PT_SETUP gp = {0};
        run_case("GCVM_PT_SETUP: zeroed",
                 IOCTL_AMDBC250_GCVM_PT_SETUP, &gp, sizeof(gp), &gp, sizeof(gp));

        gp.PageTableCount = 0xFFFFFFFF;
        gp.Flags = 0xFFFFFFFF;
        run_case("GCVM_PT_SETUP: count=0xFFFFFFFF flags=0xFFFFFFFF",
                 IOCTL_AMDBC250_GCVM_PT_SETUP, &gp, sizeof(gp), &gp, sizeof(gp));
    }

    /* ------------------------------------------------------------------ */
    /* SDMA_SELFTEST                                                     */
    /* ------------------------------------------------------------------ */
    printf("\n--- SDMA_SELFTEST ---\n");
    {
        AMDBC250_IOCTL_SDMA_SELFTEST ss = {0};
        run_case("SDMA_SELFTEST: default",
                 IOCTL_AMDBC250_SDMA_SELFTEST, &ss, sizeof(ss), &ss, sizeof(ss));

        ss.Pattern = 0xFFFFFFFF;
        ss.Size = 0xFFFFFFFF;
        run_case("SDMA_SELFTEST: Pattern=0xFFFFFFFF Size=0xFFFFFFFF",
                 IOCTL_AMDBC250_SDMA_SELFTEST, &ss, sizeof(ss), &ss, sizeof(ss));
    }

    /* ------------------------------------------------------------------ */
    /* Results                                                            */
    /* ------------------------------------------------------------------ */
    printf("\n=== SUMMARY ===\n");
    printf("PASS   : %ld\n", g_pass);
    printf("FAIL   : %ld\n", g_fail);
    printf("CRASH  : %ld\n", g_crashes);

    if (g_crashes > 0)
        printf("\n!!! WARNING: driver crashed during robustness test !!!\n");

    CloseHandle(g_hDev);
    return (g_crashes == 0) ? 0 : 1;
}

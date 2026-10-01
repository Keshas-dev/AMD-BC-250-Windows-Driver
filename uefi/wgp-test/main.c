/*
 * main.c - BC-250 UEFI WGP unlock probe.
 *
 * Derived from Hexxeh/bc250-efi-core-unlock (MIT). That project proves the SMU
 * secure-access chain works in the UEFI phase; this entry point keeps that chain
 * verbatim and adds the WGP experiment on top of it.
 *
 * Why UEFI: in Windows, host BAR5 writes to SPI_PG_ENABLE_STATIC_WGP_MASK
 * (0x5C3C) are silently dropped - proved with nine GRBM_GFX_INDEX bank
 * selectors, all read back 0, with MMIO proven working via the SCRATCH beacon
 * and the GRBM index echo. The gate is closed before x86 starts.
 *
 * Everything printed is teed to the console and to a log file on the volume the
 * probe booted from, so the result set can be read off the USB stick instead of
 * photographed. The log path had never actually been exercised before, because
 * the earlier builds were never loaded by the firmware at all.
 *
 * Nothing here patches SMU firmware. That is a separate experiment.
 */

#include <efi.h>
#include "smu.h"
#include "unlock.h"
#include "msvc_compat.h"
#include "elog.h"
#include "wgp.h"

/*
 * 0 = full probe, log  WGP.LOG
 * 1 = only the SMU unlock chain and the baseline, log  WGP0.LOG
 * 2 = chain + wgp_probe, log  WGP1.LOG
 * Override at build time with -DTEST_MODE=N.
 */
#ifndef TEST_MODE
#define TEST_MODE 0
#endif

/* Seconds to stay on screen before booting the OS. Long enough to photograph or
 * retype the results, and the probe returns EFI_SUCCESS rather than halting, so
 * there is no way to end up stuck in a halt loop. */
#define STALL_SECONDS 20

#define puts(st, s)       elog_str((st), (s))
#define putnum(st, v)     elog_u32((st), (v))
#define puthex32(st, v)   elog_hex32((st), (v))

/* ------------------------------------------------------------------------- */

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    unsigned int before, after, mask;
    EFI_STATUS status;
    const char *logname = (TEST_MODE == 1) ? "WGP0.LOG"
                         : (TEST_MODE == 2) ? "WGP1.LOG"
                                            : "WGP.LOG";

    /* Open the log first so even an early failure is recorded. */
    if (!elog_open(SystemTable, ImageHandle, logname))
        logname = 0;                    /* console only */
    elog_header(SystemTable);

    puts(SystemTable, "log file    = ");
    puts(SystemTable, logname ? logname : "(none writable, console only)");
    puts(SystemTable, "\r\n");
    putnum(SystemTable, (unsigned int)TEST_MODE);
    puts(SystemTable, "\r\n");

    /* --- Baseline ------------------------------------------------------ */
    before = smn_rd(MASK_REG);
    mask = before & 0xFFu;
    puts(SystemTable, "core mask SMN 0x0115A870 = ");
    puthex32(SystemTable, mask);
    puts(SystemTable, "\r\n");

    puts(SystemTable, "feature mask  Q0 0x3D    = ");
    if (smu_send_msg_q0(SystemTable, 0x3D, NULL, 0, &after)) puthex32(SystemTable, after);
    else puts(SystemTable, "(timeout)");
    puts(SystemTable, "\r\n");

    puts(SystemTable, "active WGP   Q0 0x1E    = ");
    if (smu_send_msg_q0(SystemTable, 0x1E, NULL, 0, &after)) putnum(SystemTable, after);
    else puts(SystemTable, "(timeout)");
    puts(SystemTable, "\r\n");

    /* --- The proven secure-access chain ------------------------------- */
    puts(SystemTable, "\r\n-- SMU secure access chain --\r\n");
    status = unlock_smu(SystemTable);
    if (EFI_ERROR(status)) {
        puts(SystemTable, "ERROR: unlock_smu failed\r\n");
        SystemTable->BootServices->Stall(STALL_SECONDS * 1000000u);
        elog_close();
        return status;
    }
    puts(SystemTable, "unlock_smu: OK\r\n");

    /* Prove the privileged SMN path is really live, not just unlocked. */
    {
        int st = sec_smn_read32(SystemTable, 0x0005A870u, &after);
        puts(SystemTable, "sec_smn_read32(0x5A870) = ");
        if (st == 0x01) puthex32(SystemTable, after & 0xFFu);
        else puts(SystemTable, "FAILED");
        puts(SystemTable, "\r\n");
    }

#if TEST_MODE == 1
    puts(SystemTable, "\n==== chain only, no WGP experiment ====\r\n");
#elif TEST_MODE == 2
    puts(SystemTable, "\n-- WGP experiment --\r\n");
    wgp_probe(SystemTable);
    puts(SystemTable, "\r\n==== probe done ====\r\n");
#else
    puts(SystemTable, "\n-- WGP experiment --\r\n");
    wgp_probe(SystemTable);

    puts(SystemTable, "\r\n-- final state --\r\n");
    puts(SystemTable, "active WGP   Q0 0x1E    = ");
    if (smu_send_msg_q0(SystemTable, 0x1E, NULL, 0, &after)) putnum(SystemTable, after);
    else puts(SystemTable, "(timeout)");
    puts(SystemTable, "\r\n");
    puts(SystemTable, "SMN 0x0005C3C          = ");
    {
        int st = sec_smn_read32(SystemTable, 0x0005C3Cu, &after);
        if (st == 0x01) puthex32(SystemTable, after);
        else puts(SystemTable, "(unreadable)");
    }
    puts(SystemTable, "\r\n");
    puts(SystemTable, "\r\n==== probe done ====\r\n");
#endif

    putnum(SystemTable, STALL_SECONDS);
    puts(SystemTable, "s, then booting the OS\r\n");

    /* Flush and close before the stall: after this the OS takes over and the
     * log must already be on disk. */
    elog_close();
    SystemTable->BootServices->Stall(STALL_SECONDS * 1000000u);

    return EFI_SUCCESS;
}

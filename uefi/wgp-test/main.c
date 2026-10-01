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
 * and the GRBM index echo. The gate is closed before x86 starts. In the UEFI
 * phase that gate is not up yet, and a cold boot is a guaranteed clean return.
 *
 * The question this answers is narrow and testable: does SPI_PG accept a write
 * in the UEFI phase, and if it does, does ActiveWgp (Q0 0x1E) move off zero?
 *
 * It does NOT patch SMU firmware. That is a separate experiment.
 */

#include <efi.h>
#include "smu.h"
#include "unlock.h"
#include "msvc_compat.h"
#include "elog.h"
#include "wgp.h"

/* 0 = report and continue booting, 1 = report and halt (no reboot risk) */
#define TEST_MODE 1

/* Console + file output all goes through elog, so the log file gets an exact
 * copy of everything shown on screen. */
#define puts(st, s)       elog_str((st), (s))
#define putnum(st, v)     elog_u32((st), (v))
#define puthex32(st, v)   elog_hex32((st), (v))

/* ------------------------------------------------------------------------- */

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    unsigned int before, after, mask;
    EFI_STATUS status;
    int st;

    (void)ImageHandle;

    elog_init(SystemTable, ImageHandle);
    elog_banner(SystemTable);

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
        SystemTable->BootServices->Stall(8000000);
        elog_close(SystemTable);
        return status;
    }
    puts(SystemTable, "unlock_smu: OK\r\n");

    /* Prove the privileged SMN path is really live, not just unlocked. */
    st = sec_smn_read32(SystemTable, 0x0005A870u, &after);
    puts(SystemTable, "sec_smn_read32(0x5A870) = ");
    if (st == 0x01) puthex32(SystemTable, after & 0xFFu);
    else puts(SystemTable, "FAILED");
    puts(SystemTable, "\r\n");

    /* --- The actual WGP experiment ------------------------------------ */
    puts(SystemTable, "\r\n-- WGP experiment --\r\n");
    wgp_probe(SystemTable);

    puts(SystemTable, "\r\n-- final state --\r\n");
    puts(SystemTable, "active WGP   Q0 0x1E    = ");
    if (smu_send_msg_q0(SystemTable, 0x1E, NULL, 0, &after)) putnum(SystemTable, after);
    else puts(SystemTable, "(timeout)");
    puts(SystemTable, "\r\n");
    puts(SystemTable, "SMN 0x0005C3C (SPI_PG?) = ");
    st = sec_smn_read32(SystemTable, 0x0005C3Cu, &after);
    if (st == 0x01) puthex32(SystemTable, after);
    else puts(SystemTable, "(unreadable)");
    puts(SystemTable, "\r\n");

    puts(SystemTable, "\r\n-- end of probe --\r\n");

#if TEST_MODE
    puts(SystemTable, "\r\nTEST_MODE: halting. Power off when done.\r\n");
    /* Close before stalling so the log file is fully flushed - we never return. */
    elog_close(SystemTable);
    SystemTable->BootServices->Stall(15000000);
    while (1) halt_cpu();
#else
    elog_close(SystemTable);
    return EFI_SUCCESS;
#endif
}

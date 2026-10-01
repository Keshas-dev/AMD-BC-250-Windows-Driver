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
 * Console output only. The elog file-logging and early run-marker builds were
 * removed from this binary: neither of them produced any output on this board,
 * and neither added anything the console did not already have.
 *
 * Nothing here patches SMU firmware. That is a separate experiment.
 */

#include <efi.h>
#include "smu.h"
#include "unlock.h"
#include "msvc_compat.h"
#include "wgp.h"

/*
 * Seconds to stay on screen before booting the OS. Long enough to photograph or
 * retype the results, and the probe returns EFI_SUCCESS rather than halting, so
 * there is no way to end up stuck in a halt loop.
 */
#define STALL_SECONDS 20

/* ------------------------------------------------------------------------- */
/* Console output helpers. The UEFI console takes UTF-16, so ASCII literals  */
/* are widened here.                                                           */
/* ------------------------------------------------------------------------- */

static void puts(EFI_SYSTEM_TABLE *st, const char *s)
{
    UINT16 buf[160];
    int i = 0;
    for (; *s && i < 159; s++) buf[i++] = (UINT16)(unsigned char)*s;
    buf[i] = 0;
    print(st, buf);
}

static void putnum(EFI_SYSTEM_TABLE *st, unsigned int v)
{
    UINT16 b[16];
    char t[12];
    int n = 0, i;
    if (!v) { b[0] = '0'; b[1] = 0; print(st, b); return; }
    while (v && n < 10) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (i = 0; i < n; i++) b[i] = (UINT16)t[n - 1 - i];
    b[n] = 0;
    print(st, b);
}

static void puthex32(EFI_SYSTEM_TABLE *st, unsigned int v)
{
    /* print_hex already emits the 0x prefix - do not add another. An earlier
     * build printed "0x0x000000FF" for every value because of it. */
    print_hex(st, v);
}

/* ------------------------------------------------------------------------- */

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    unsigned int before, after, mask;
    EFI_STATUS status;

    (void)ImageHandle;

    puts(SystemTable, "\r\n");
    puts(SystemTable, "==============================================\r\n");
    puts(SystemTable, " BC-250 UEFI WGP UNLOCK PROBE\r\n");
    puts(SystemTable, "==============================================\r\n");

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
        SystemTable->BootServices->Stall(20000000);
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

    /* --- The actual WGP experiment ------------------------------------ */
    puts(SystemTable, "\r\n-- WGP experiment --\r\n");
    wgp_probe(SystemTable);

    puts(SystemTable, "\r\n-- final state --\r\n");
    puts(SystemTable, "active WGP   Q0 0x1E    = ");
    if (smu_send_msg_q0(SystemTable, 0x1E, NULL, 0, &after)) putnum(SystemTable, after);
    else puts(SystemTable, "(timeout)");
    puts(SystemTable, "\r\n");
    puts(SystemTable, "SMN 0x0005C3C (SPI_PG?) = ");
    {
        int st = sec_smn_read32(SystemTable, 0x0005C3Cu, &after);
        if (st == 0x01) puthex32(SystemTable, after);
        else puts(SystemTable, "(unreadable)");
    }
    puts(SystemTable, "\r\n");

    puts(SystemTable, "\r\n==== probe done ====\r\n");
    putnum(SystemTable, STALL_SECONDS);
    puts(SystemTable, "s, then booting the OS\r\n");

    /* Stall so the output can be read, then chainload. No halt loop: a halt
     * cannot be escaped from the firmware and there is no reason to risk
     * ending up wedged in it. */
    SystemTable->BootServices->Stall(STALL_SECONDS * 1000000u);

    return EFI_SUCCESS;
}

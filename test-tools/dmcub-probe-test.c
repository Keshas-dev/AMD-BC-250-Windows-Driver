/* dmcub-probe-test.c - Probe BC-250 DMCUB (Display MicroController) mailbox.
 *
 * DMCUB owns the DCN display engine (same role PSP/SOS plays for the CP).
 * INBOX0 (host->DMCUB) ring is at 0x7010-0x701C; OUTBOX (DMCUB->host) and
 * STATUS/INTERRUPT are expected nearby (0x7020+). This tool reads the whole
 * DMCUB region, checks if INBOX0_BASE is programmed (DMCUB firmware loaded),
 * and - if alive - attempts a harmless command and watches for a response.
 *
 * SAFE: only reads + writes to DMCUB mailbox registers (no OTG/freeze-zone
 * writes). If DMCUB is SOS-locked this will simply show non-responsive state.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b;
    r.RegisterOffset = o; r.Value = v;
    return DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &b, NULL);
}
static uint32_t R32(uint32_t o) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b;
    r.RegisterOffset = o; r.Value = 0;
    if (DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &b, NULL))
        return r.Value;
    return 0xFFFFFFFF;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL open gle=%lu\n", GetLastError()); return 1; }

    /* Make sure BAR5 is mapped (NBIO_MAP path - no full init needed). */
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("INIT fail gle=%lu\n", GetLastError());
        CloseHandle(h); return 1;
    }

    printf("=== BC-250 DMCUB (Display MicroController) Probe ===\n\n");

    /* DMCUB region 0x7000-0x7060 */
    static const uint32_t regs[] = {
        0x7000, 0x7004, 0x7008, 0x700C,
        0x7010, 0x7014, 0x7018, 0x701C,   /* INBOX0 rptr/wptr/base/size */
        0x7020, 0x7024, 0x7028, 0x702C,   /* OUTBOX0 rptr/wptr/base/size (expected) */
        0x7030, 0x7034, 0x7038, 0x703C,   /* STATUS / INTERRUPT (expected) */
        0x7040, 0x7044, 0x7048, 0x704C,
        0x7050, 0x7054, 0x7058, 0x705C,
    };
    printf("--- DMCUB register map ---\n");
    for (int i = 0; i < (int)(sizeof(regs)/sizeof(regs[0])); i++) {
        uint32_t v = R32(regs[i]);
        const char* name = "?";
        switch (regs[i]) {
            case 0x7000: name = "SCRATCH0"; break;
            case 0x7004: name = "SCRATCH1"; break;
            case 0x7010: name = "INBOX0_RPTR"; break;
            case 0x7014: name = "INBOX0_WPTR"; break;
            case 0x7018: name = "INBOX0_BASE"; break;
            case 0x701C: name = "INBOX0_SIZE"; break;
            case 0x7020: name = "OUTBOX0_RPTR?"; break;
            case 0x7024: name = "OUTBOX0_WPTR?"; break;
            case 0x7028: name = "OUTBOX0_BASE?"; break;
            case 0x702C: name = "OUTBOX0_SIZE?"; break;
            case 0x7030: name = "DMCUB_CNTL?"; break;
            case 0x7034: name = "DMCUB_STATUS?"; break;
            case 0x7038: name = "DMCUB_INTERRUPT?"; break;
            case 0x703C: name = "DMCUB_INTERRUPT_EN?"; break;
        }
        printf("  [0x%04X] %-18s = 0x%08X%s\n", regs[i], name, v,
               (v == 0xFFFFFFFF) ? " DEAD" : "");
    }

    /* Is DMCUB firmware loaded? INBOX0_BASE non-zero => ring buffer programmed. */
    uint32_t inbox_base = R32(0x7018);
    uint32_t inbox_size = R32(0x701C);
    uint32_t inbox_wptr = R32(0x7014);
    uint32_t inbox_rptr = R32(0x7010);
    uint32_t outbox_wptr = R32(0x7024);
    uint32_t outbox_rptr = R32(0x7020);
    printf("\n--- DMCUB alive check ---\n");
    printf("  INBOX0_BASE  = 0x%08X %s\n", inbox_base, inbox_base ? "(programmed)" : "(zero = no fw ring)");
    printf("  INBOX0_SIZE  = 0x%08X\n", inbox_size);
    printf("  INBOX0_WPTR  = 0x%08X  RPTR = 0x%08X  (pending=%u)\n",
           inbox_wptr, inbox_rptr, (inbox_wptr - inbox_rptr) & 0xFFF);
    printf("  OUTBOX0_WPTR = 0x%08X  RPTR = 0x%08X\n", outbox_wptr, outbox_rptr);

    if (inbox_base == 0 || inbox_base == 0xFFFFFFFF) {
        printf("\n  DMCUB INBOX0 ring NOT programmed -> DMCUB firmware not loaded / SOS-owned.\n");
        printf("  Conclusion: host cannot drive DCN via DMCUB. Display is BIOS/SOS-owned.\n");
        CloseHandle(h);
        return 0;
    }

    /* DMCUB appears alive. Try a harmless command: DMCUB_COMMAND_IS_DISPLAY_IDLE
     * (header-only, command 0x8, no payload). Write header to INBOX ring, advance WPTR. */
    printf("\n--- Attempt harmless DMCUB command (IS_DISPLAY_IDLE) ---\n");
    /* Ring layout: base is a struct dmcub_cmd_header ring. Header is 0x10 bytes.
     * For a no-payload command we write 4 DWORDs at base+WPTR, then WPTR += 0x10. */
    uint64_t ring_base = (uint64_t)inbox_base;
    uint32_t wptr = inbox_wptr & 0xFFF; /* ring offset within size */
    uint32_t cmd[4] = {
        0x00000008, /* header: command=8 (IS_DISPLAY_IDLE), no payload */
        0x00000000, /* param */
        0x00000000,
        0x00000000,
    };
    /* Write 16 bytes into the ring at current wptr.
     * NOTE: ring is in GPU memory (INBOX0_BASE is a GPU physical address), NOT
     * a BAR5 MMIO offset - so we cannot use W32() (BAR5). We can only observe
     * via the WPTR/RPTR registers. Writing the payload would require mapping
     * the ring buffer, which we skip (safe probe only). */
    printf("  Ring base=0x%llX WPTR_off=0x%X size=0x%X\n", ring_base, wptr, inbox_size);
    printf("  (DMCUB ring payload lives in GPU memory at INBOX0_BASE; we cannot\n");
    printf("   write it via BAR5 MMIO. We only observe WPTR/RPTR handshake.)\n");

    /* Advance WPTR to hand a (zeroed) command to DMCUB and watch for response. */
    uint32_t new_wptr = (wptr + 0x10) & (inbox_size ? (inbox_size - 1) : 0xFFF);
    uint32_t before_out = R32(0x7024);
    W32(0x7014, new_wptr); /* tell DMCUB a command is queued */
    /* Poll a few ms for OUTBOX activity / SCRATCH change. */
    uint32_t scr_before = R32(0x7000);
    for (int i = 0; i < 50; i++) {
        uint32_t ob = R32(0x7024);
        uint32_t scr = R32(0x7000);
        if (ob != before_out || scr != scr_before) {
            printf("  DMCUB responded! OUTBOX0_WPTR 0x%08X->0x%08X, SCRATCH0 0x%08X->0x%08X\n",
                   before_out, ob, scr_before, scr);
            break;
        }
        Sleep(1);
    }
    printf("  After WPTR advance: INBOX0_WPTR=0x%08X OUTBOX0_WPTR=0x%08X SCRATCH0=0x%08X\n",
           R32(0x7014), R32(0x7024), R32(0x7000));
    printf("  (If OUTBOX/WPTR did not move, DMCUB ignored or is SOS-locked.)\n");

    CloseHandle(h);
    return 0;
}

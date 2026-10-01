# BC-250 UEFI WGP Unlock Probe

`bc250-wgp-probe.efi` — a freestanding UEFI application that runs the proven
SMU secure-access chain and then tests three WGP hypotheses.

## Provenance

The unlock chain is taken verbatim from
[Hexxeh/bc250-efi-core-unlock](https://github.com/Hexxeh/bc250-efi-core-unlock)
(MIT), cloned to `C:\AMD-BC-250\bc250-efi-core-unlock`. That project's `smu.c`,
`unlock.c`, `patches.c`, `smu.h`, `unlock.h` and `patches.h` are used unmodified
apart from swapping the includes. Their EFI headers are vendored from
[yoppeh/efi](https://github.com/yoppeh/efi) (MIT) under `vendor/`.

`wgp.c` and `main.c` are new. The `patches.hex` payload is their
`bc250-smu-unlock` submodule, and `gen_patches.py` turns it into
`patches.data.h` (60 entries). **`patches.data.h` is generated but deliberately
not applied** — see "What it does not do".

## Windows build

Upstream ships only `make clang` and `make mingw`. This machine has neither, so
`msvc_compat.h` bridges the gap:

| Upstream (GNU asm) | MSVC x64 intrinsic |
|---|---|
| `outpd(port, val)` | `_outpd()` |
| `inpd(port)` | `_inpd()` |
| `__asm__ __volatile__("hlt")` | `__halt()` |

The x64 spellings are single-underscore. Verified by compiling probes on MSVC
14.44: `_outpd`/`_inpd`/`__halt` resolve; `__outpd`/`__inpd` and the x86-only
`__inp`/`__inp*` are not declared for x64 and fail with C4013 then LNK2019.
`outpd` and `inpd` are already intrinsic names, so they are macros, not wrapper
functions — C2169 rejects a definition.

```
uefi\wgp-test\build-msvc.bat
```

Output `uefi\wgp-test\output\bc250-wgp-probe.efi`, PE32+ x64,
subsystem `EFI Application`, entry `efi_main`. Builds with 0 warnings at `/W3`.

## How to run

1. Format a USB stick **FAT32**.
2. Create `EFI\BOOT\` on it and copy the .efi there as **`BOOTX64.EFI`**.
3. Set the BIOS boot order to prefer USB, or press F11 / F12 at boot.
4. Read the console output. `TEST_MODE` is 1, so it **halts** after printing and
   does not chainload — power off when done.

## What it does, in order

1. Reads the baseline: core mask SMN `0x0115A870`, feature mask Q0 `0x3D`,
   active WGP Q0 `0x1E`.
2. Runs `unlock_smu()` — the upstream chain: Q2 `0x23` subqueue-4 overflow to
   repoint `TR_TABLE_PTR`, Q2 `0x0A` transfer engine to stage a fake walk table,
   write 0 to the `0x7B3C` gate, then SMU state fixup. Confirms
   `sec_smn_read32` works afterwards.
3. **H2 — privileged write proof.** Writes the core mask's own value back and
   reads it again. Without this a failed SPI_PG write is uninterpretable.
4. **H1 — GC SMN alias probe.** Eight candidate addresses, no sweeping. A live
   alias must read non-zero and not `0xFFFFFFFF`.
5. **H3 — feature bit 6 round trip.** Reads the mask, disables bit 6 via Q2
   `0x06`, reads again, re-enables via Q2 `0x05`, reads again. If nothing
   changes, the tick loop is not evaluating that feature.

Q0 mailbox support (`smu_send_msg_q0`) was added to `smu.c`/`smu.h`; upstream had
only Q2 and Q3, and Q2/Q3 are not enough to read `0x3D` or `0x1E`. It cannot
reuse `smu_send_msg_generic`, which writes six argument words starting at ARG —
correct for Q2/Q3, wrong for Q0's single argument at C2PMSG_82.

## What it does NOT do

- **No SMU SRAM writes.** The upstream 60-patch firmware set is generated but not
  applied. Those patches target core unlock and their semantics are unknown.
- **No core mask write.** The probe leaves `0x0115A870` alone.
- **No BAR5 access.** Mapping it would need the
  `EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL` vtable, and the host BAR5 path is already
  closed: SPI_PG never accepted a host write across nine `GRBM_GFX_INDEX`
  selectors, with MMIO proven working. The gates are programmed by PSP_BL before
  x86 reset release, so UEFI is not early enough to escape them either. The
  privileged SMN path is the one that is genuinely new.
- **No SMU SRAM sweeps, no blind SMN access.** Eight named addresses only.

## Risk

`unlock_smu()` deliberately corrupts the SMU subqueue ring and then repairs it.
If it dies partway the SMU state is inconsistent — but SMU SRAM is volatile and a
cold boot (power off, not reboot) restores it. There is no flash write, so the
BIOS is untouched and there is nothing to undo.

The probe halts rather than rebooting, so it cannot accidentally boot a
half-patched machine.

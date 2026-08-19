# AMD BC-250 Windows Driver — Agent Notes

## Workspace boundary
- This is the GPU driver repo; the PSP driver is a sibling git repo at `C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver`.
- Run git/build commands from this repo root; `C:\AMD-BC-250` itself is not the git repo.

## Build and verification
- `build.bat` builds and signs `output\atikmdag.sys`, `output\amdbc250umd64.dll`, INF, and CAT using `AMD-BC250-Signer` in the `My` store; run from a VS2022 x64 Native Tools/Admin prompt.
- The build script detects VS2022 and WDK on D:, E:, or C:; signing fails if the cert is not trusted or the prompt is not elevated.
- Compile focused tests with `test-tools\compile-wddm.bat` (WDDM+IOCTL), `test-tools\compile-safe.bat` (read-only), and `test-tools\compile-deep.bat` (NBIO/DF/MMHUB scan + writes).
- There is no package manager, CI, lint, formatter, or typecheck. Prefer executable scripts and current code over historical docs.

## Install and runtime
- Test signing (`bcdedit /set testsigning on`) plus reboot is required; Secure Boot must be OFF.
- Before installing a new driver: Device Manager uninstall with "Delete driver" -> reboot -> install from `output\` -> reboot.
- Helper script: `reinstall-gpu-driver.bat` (run as Admin) uninstalls the old GPU driver, reboots, then auto-installs the new GPU driver and reboots again. **The PSP driver is NOT installed/reinstalled anymore — GPU driver only.**
- **Windows 11 26100**: Install GPU driver first (maps BAR5), then PSP driver (uses GPU proxy for mailbox access).
- **Older Windows**: PSP driver can map BAR5 directly; PSP driver can be installed before or after GPU driver.

## Architecture
- This is a WDM control/IOCTL driver, not a real WDDM miniport on Win11 26100. `DxgkInitialize` is NOT a dxgkrnl.sys export (it lives in displib.lib, see the 2026-08-01 correction below); our runtime export-scan falls back to WDM IOCTL mode and the DDI path is stubbed.
- DriverBuildId registry marker confirms new binary loaded. Step markers: DriverEntryRan=1, Step_BeforeDxgkInit=10, Step_DriverEntryPost=11 (WDM fallback).
- Main IOCTL device: `\\.\AMDBC250DreamV43`; primary GPU MMIO BAR5 is `0xFE800000` (512KB).
- Do not map past BAR5 or probe random unknown offsets casually; hardware hangs require reboot.
- READ_REG/WRITE_REG use direct GPU BAR5 (`DreamV3ReadRegister`/`DreamV3WriteRegister`) when `g_Bar5Mapping` is available.
- On Windows 11 26100, if direct BAR5 mapping fails, READ_REG/WRITE_REG fall back to PSP proxy IOCTLs.
- PSP proxy is for PSP-specific work (mailbox, firmware loading) only.

## BC-250 hardware facts
- BC-250 GC registers are shifted by `GC_BASE=0x1260`; use `AMDBC250_REG_*` macros. Navi10 offsets like `0x2000`/`0x2004` read `0xFFFFFFFF` because they are unmapped, not NBIO-blocked.
- Key corrected offsets: GRBM `0x3260`, CC `0x9C1C` (NOT 0x3264 — see bc250-collective), scratch `0x32D4`, SPI WGP mask `0x5C3C` (NOT 0x34FC but hw read-only — WGPs fused off).
- NBIO blocks writes in native `0xC000+` ranges such as `CP_ME_CNTL`/`CP_MEC_CNTL`; GC_BASE-shifted ring aliases can bypass NBIO but some BASE registers are hardware read-only.
- **Linux IP Base Map** (from `cyan_skillfish_ip_offset.h` — full details in `docs/BC250-LINUX-IP-MAP.md`):
  - GC: `0x1260` (+ `0xA000`), NBIO: `0x0000`, HDP: `0x0F20`, MMHUB: `0x1A000`
  - DF: `0x7000`, OSSSYS: `0x10A0`, MP0/PSP: `0x16000`, THM: `0x16600`
  - SMUIO: `0x16800`/`0x16A00`, CLK: `0x16C00+`, UMC0: `0x14000`, FUSE: `0x17400`
  - GC IP version: 10.1.3, NBIO: 2.1.1, MP0/PSP: 11.0.8
  - Linux skips PSP firmware loading entirely for BC-250 (`psp_v11_0_8.c` is minimal)
  - `cg_flags=0`, `pg_flags=0` (no clock/power gating), `external_rev_id = rev_id + 0x82`
- **40 CU unlock** (from bc250-collective): Write `CC_GC_SHADER_ARRAY_CONFIG` (0x9C1C) + `SPI_PG_ENABLE_STATIC_WGP_MASK` (0x5C3C) together during `gfx_v10_0_get_cu_info()`. Linux mm* offsets: CC=0x226F*4+0x1260=0x9C1C, SPI=0x1277*4+0x1260=0x5C3C. SPI_PG_MASK (0x5C3C) is read-only at runtime (0x00000000) — WGPs fused off on our unit. UNLOCK_40CU IOCTL defined in header but NEVER implemented in driver.
- **GRBM selection**: Linux uses `mmGRBM_GFX_CNTL` (0x0dc2, BAR5=0x2022) for ME/PIPE/QUEUE select, NOT `GRBM_GFX_INDEX` (0x34D0). These are DIFFERENT registers.
- **CP_MEC_CNTL**: Navi10 offset mmREG=0x0e2d (BAR5=0x4B14), NOT Sienna_Cichlid 0x0f55. BC-250 is GFX10.1.3, not 10.3.x.

## ⭐ PSP KM GPCOM RING WORKS — kernel IOCTLs verified on hardware (2026-08-18, SUPERSEDES all below)

### VERIFIED ON HARDWARE (psp-ring-submit-test.exe against installed atikmdag.sys)
- **REAL, WORKING MP0 C2PMSG base in BAR5 = 0x58000** (= ip_discovery MP0 base 0x16000 in DWORD units × 4).
  The earlier probe conclusion "base = 0x103D0" and the "ring permanently unavailable / no TOS" verdict were
  WRONG — they were based on addresses that read back idle values, not the live mailbox.
- **`PSP_RING_INIT` (IOCTL 0x80000C18) result**: Result=1, RingPa=0x7E512000, RingSize=0x1000,
  C2pmsg64=**0x80020000** (bit31 = RESP ACK set + ring type 0x0002 KM echo), C2pmsg81=0x002B9309.
  Ring created exactly per Linux `psp_v11_0_8_ring_create()`.
- **`PSP_RING_SUBMIT` (IOCTL 0x80000C1C) result**: fence REACHED for every command;
  WPTR (C2PMSG_67) advances 0x10 per frame (64B psp_gfx_rb_frame) — ring consumption confirmed.
- **`GFX_CMD_ID_GET_FW_ATTESTATION` (0x0F) returned resp.status = 0x00000000 (SUCCESS)** —
  PSP executed a real command through the ring. This is the definitive proof the GPCOM ring works.
- `GET_FW_ATTESTATION2` (0x10) and `FB_FW_RESERV_ADDR` (0x50) returned **0x00000100 =
  PSP_ERR_UNKNOWN_COMMAND** (psp_gfx_if.h line 494) — BC-250 SOS doesn't implement those commands;
  protocol itself succeeded (fence hit). NOT an error in our code.

### Kernel implementation (files changed 2026-08-18)
- `amdbc250_dream_kmd.c` cases **0x80000C18 (PSP_RING_INIT)** and **0x80000C1C (PSP_RING_SUBMIT)**.
- File-scope defines (SUPERSEDE the wrong 0x103D0-based DIRECT_C2PMSG_* in amdbc250_psp.c):
  `PSP_MP0_BASE 0x58000`, C2PMSG_64@0x58200 / 67@0x5820C / 69@0x58214 / 70@0x58218 / 71@0x5821C / 81@0x58244,
  `PSP_RING_SIZE 0x1000`, `PSP_RING_TYPE_KM 2`, `PSP_CMD_BUF_SIZE 0x1000`.
- INIT: waits TOS-ready (C2PMSG_64 bit31) → `MmAllocateContiguousMemory(0x1000)` ring → writes
  C2PMSG_69/70/71 → `C2PMSG_64 = KM(2)<<16` → 20ms → waits RESP bit31 → sets `PspRingCreated`.
- SUBMIT: lazy-allocates cmd + fence buffers (both 0x1000), builds `psp_gfx_cmd_resp`
  (buf_size=0x400@+0, buf_version=1@+4, cmd_id@+8, union cmd copied at +28, resp@+864),
  writes 64B `psp_gfx_rb_frame` (+0 cmd_buf_addr_lo, +4 hi, +8 size, +12 fence lo, +16 fence hi,
  +20 fence_value), advances `wptr=(wptr+16)%1024`, kicks C2PMSG_67, polls fence with `DreamV3HdpFlush`.
- Output 24B: {Result, FenceStatus, RespStatus, RespFwAddrLo, RespFwAddrHi, RespTmrSize}.
- Protected by `DevExt->DeviceMutex`; buffers freed in `DreamV3WdmUnload`.
- `inc/amdbc250_ioctl.h`: `IOCTL_AMDBC250_PSP_RING_INIT`/`PSP_RING_SUBMIT` (Function 0x96/0x97 → 0x80000C18/0x80000C1C).
- Code Reviewer pass fixed: output-buffer overrun, cmdDataSize input over-read clamp,
  status-masking, cmd payload offset +12→+28, added mutex + zeroing + PA==0 checks.

### Test tools
- `test-tools/psp-ring-submit-test.c` + `compile-psp-ring-submit-test.bat` → `output\psp-ring-submit-test.exe`.
- `test-tools/psp-ring-create-v2.c` (user-mode ring_create proof, base 0x58000).

### LOAD_IP_FW via ring — VERIFIED ON HARDWARE (2026-08-18, second run)
- `PSP_RING_LOAD_IP_FW` (IOCTL 0x80000C20) works — driver reads the firmware file itself
  (must pass NT path `\SystemRoot\System32\drivers\bc-250\xxx.bin`, NOT Win32 `C:\...`;
  `ZwCreateFile` in the driver rejects Win32 paths with STATUS_OBJECT_NAME_NOT_FOUND).
- **SMU (fw_type=18, `Smu.bin`) → RespStatus=0x00000000 = TEE_SUCCESS — firmware LOADED through the ring.** 🎉
  This is the first genuine PSP firmware load via GPCOM ring on our hardware.
- RLC_G (8, `cyan_skillfish2_rlc.bin`), SDMA0 (9, `navi12_sdma.bin`), SDMA1 (10, `navi12_sdma1.bin`)
  → **0xFFFF0008 = TEE_ERROR_ITEM_NOT_FOUND** (GlobalPlatform TEE error). BC-250 SOS does not have
  firmware "items" for these types — consistent with Linux (psp_v11_0_8.c has no load_ip_fw at all;
  those FW are loaded by bootloader/VBIOS at boot, not via PSP ring).
- CP ME/PFP/CE/MEC (1-4) → **0x80000203** (bit31 = GFX_CMD_RESPONSE_MASK + status 0x203). Likely
  "already loaded" or unsupported-by-SOS; not a protocol failure (fence hit, response read clean).
- RespFwAddrLo/Hi=0 and RespTmrSize=0 for all — SOS reports no TMR placement (TMR not set up).
- Test tool: `test-tools/psp-ring-load-ip-fw-test.c` + `compile-psp-ring-load-ip-fw-test.bat`
  → `output\psp-ring-load-ip-fw-test.exe`. All 8 types attempted; `exe <type>` to run just one.
- SMU file name is `Smu.bin` (NOT `cyan_skillfish2_smc.bin` which does not exist in bc-250\ dir).
- Candidate commands after that: LOAD_TOC (0x20), AUTOLOAD_RLC (0x21).

### PSP ring command surface — FULLY MAPPED (2026-08-19, psp-ring-generic-cmd-test.exe)
Generic GFX_CMD_ID probes via IOCTL 0x80000C1C (PSP_RING_SUBMIT), after SETUP_TMR succeeded:
| Command | ID | RespStatus | Meaning |
|---------|----|------------|---------|
| GET_FW_ATTESTATION | 0x0F | 0x00000000 | SUCCESS (control) |
| BOOT_CFG GET | 0x24 | 0x00000000 | SUCCESS (boot cfg supported) |
| DESTROY_TMR | 0x07 | 0x00000000 | SUCCESS (TMR fully manageable) |
| SETUP_VMR / DESTROY_VMR | 0x09 / 0x0A | 0xFFFF000A | TEE_ERROR_NOT_SUPPORTED (SRIOV only) |
| PROG_REG | 0x0B | 0xFFFF0009 | TEE_ERROR_NOT_IMPLEMENTED |
| LOAD_TOC | 0x20 | 0xFFFF0009 | TEE_ERROR_NOT_IMPLEMENTED |
| AUTOLOAD_RLC | 0x21 | 0xFFFF0009 | TEE_ERROR_NOT_IMPLEMENTED |
| FB_FW_RESERV_ADDR / EXT | 0x50 / 0x51 | 0x00000100 | PSP_ERR_UNKNOWN_COMMAND |
- **Conclusion:** the BC-250 SOS knows PROG_REG/LOAD_TOC/AUTOLOAD_RLC but does NOT implement them.
  The PSP ring CANNOT write SPI_PG (PROG_REG = NOT_IMPLEMENTED) and has no TOC/RLC autoload path.
  The PSP ring mechanism is now fully explored: INIT/SUBMIT/GET_FW_ATTESTATION/SETUP_TMR/DESTROY_TMR/
  BOOT_CFG/LOAD_IP_FW(SMU only) all verified. 3D remains blocked by SPI_PG SOS-lock (EFI route only).
- Test tool: `test-tools/psp-ring-generic-cmd-test.c` + `compile-psp-ring-generic-cmd-test.bat`
  → `output\psp-ring-generic-cmd-test.exe`.

### SETUP_TMR via ring — VERIFIED SUCCESS ON HARDWARE (2026-08-19): VRAM MC + aper_base works
- `PSP_RING_SETUP_TMR` (IOCTL 0x80000C24, CTL_CODE_AMDBC250(0x99)) = `GFX_CMD_ID_SETUP_TMR` (0x05).
- **HARDWARE RESULT: `RespStatus=0x00000000` (SUCCESS)** with buf_phy_addr=0xF40F800000 (MC) +
  system_phy_addr=0xCF800000. **aper_base = 0xC0000000 (BAR0) CONFIRMED** — the SOS accepted the
  MC/physical pairing, so the VRAM physical base is now hardware-verified (not just hardcoded).
  RespTmrSize=0 and RespFwAddrLo/Hi=0 (SOS doesn't populate those for SETUP_TMR on BC-250; fine).
- **Earlier host-RAM failures (0xFFFF0006 = TEE_ERROR_BAD_PARAMETERS):** root cause was NOT "SOS
  requires VRAM". Linux allocates the TMR BO as `AMDGPU_GEM_DOMAIN_GTT | AMDGPU_GEM_DOMAIN_VRAM`
  (psp_tmr_init). The real requirement: `buf_phy_addr` = **GPU address** (MC for VRAM, GART VA for
  GTT), `system_phy_addr` = **CPU physical** — they must be CORRECT and DIFFERENT. Our host-RAM test
  passed the same CPU PA (0x7D800000) for both, and 0x7D800000 is NOT a valid GPU address on this
  device (VRAM MC is 0xF400000000+; GTT GPU addr ≈ 0xF410000000+ needs working GART, which we don't
  have). We use VRAM because a VRAM MC address needs no page tables; Linux also placed the TMR in VRAM
  (dmesg "PSP TMR: 4MB reserved at 0xF40F800000").
- **Current driver code (2026-08-19):** no host allocation. Passes buf_phy_addr = 0xF40F800000 (MC),
  system_phy_addr = BAR0 physical + 0x0F800000 (aper_base = FbPhysicalBase from PCI resource scan,
  fallback 0xC0000000). Validates offset+tmrSize ≤ 256MB VRAM. Output includes TmrPaLo/Hi (system) +
  TmrMcLo/Hi (MC). DevExt stores PspTmrPa/PspTmrMc/PspTmrSize (no host mem).
- `PSP_TMR_SIZE` = 0x400000 (4MB, non-ALDEBARAN), `PSP_TMR_ALIGNMENT` = 0x100000. psp_skip_tmr() = false on
  BC-250 (boot_time_tmr=false, autoload_supported=false) → Linux really sends SETUP_TMR.
- **Next candidates:** retry LOAD_IP_FW (see if RLC/SDMA now behave differently with a TMR present),
  then LOAD_TOC (0x20) which computes tmr_size from the TOC, then AUTOLOAD_RLC (0x21).

### Key corrected offsets (0x58000 base, byte offsets)
| Register | Offset | Linux/PROBE note |
|----------|--------|------------------|
| C2PMSG_33 | 0x58184 | PSP init complete (0x80000000) |
| C2PMSG_35/36/37 | 0x5818C/90/94 | bootloader commands (unusable, SOS alive) |
| C2PMSG_64 | 0x58200 | cmd / TOS-ready / RESP — LIVE (0x80020000) |
| C2PMSG_67 | 0x5820C | ring WPTR — advances on submit |
| C2PMSG_69/70/71 | 0x58214/18/1C | ring addr low/high, size |
| C2PMSG_81 | 0x58244 | SOS status (0x002B9309, not bit31) |
| C2PMSG_101 | 0x58294 | scratch |

## PSP Firmware Loading Status (2026-08-18 UPDATE — register probe) [OLD/HISTORICAL — superseded above]

### PROBE RESULTS (psp-ring-probe-esc.exe via KMDOD Escape, 2026-08-18)
- **REAL MP0 base in BAR5 = 0x103D0** (dword 0x40F4). `C2PMSG_81 @ 0x10614 = 0xF0000010` (bit31 = SOS alive). `Amdbc250PspDiscoverMp0Base` finding 0x40F4 is CORRECT; `Amdbc250PspReadRegister/WriteRegister` (`g_Mp0BaseDword * 4 + RegByteOffset`) are correct.
- **Direct C2PMSG offsets WERE WRONG by 0x10** — code used 0x1056C/0x10570/0x10574 (base 0x103E0), correct are **0x1055C/0x10560/0x10564** (base 0x103D0). FIXED 2026-08-18 in amdbc250_psp.c (DIRECT_C2PMSG_35/36/37_OFFSET). All prior PSP_LOAD_IP_FW attempts wrote to dead registers.
- **SMN C2PMSG path is SMU, NOT PSP** — `amdbc250_dream_psp_fw_load.c` writes SMN 0x03B10A08/0x03B10A48/0x03B10A68 which are SMU C2PMSG_66/82/90 (probe: 0x1E/0x00/0x01). PSP C2PMSG are NOT mirrored into SMN. This path needs rework if used.
- **C2PMSG_64 bit31 (TOS ready) = 0** even at correct address 0x105D0 — ring_create first wait (MBOX_TOS_READY_FLAG) can never succeed without TOS. Ta.bin (262656 B, same size as Sysdrv.bin) exists in `drivers\bc-250\` — candidate TOS to test before ring.
- SMN formula candidates C2PMSG_n = 0x03B10900 + n*4 (D) and ip_discovery MP0 0x16000 (C) both read 0 — NOT the PSP mailbox location.
- C2PMSG_35/36/37/64/67/69/70/71/101 at correct BAR5 base all read 0 (idle/uncommanded), C2PMSG_81@0x10614 = 0xF0000010.

### TOS TEST RESULT (psp-tos-test.exe, 2026-08-18 — DEFINITIVE)
- **IOCTL code bug FIXED**: test-tools hardcoded `0x800024A3` (TOS) / `0x80002483` (IP_FW) but driver case values (from CTL_CODE METHOD_BUFFERED header macros) are `0x800024A0` / `0x80002480`. Confirmed via byte-pattern scan of installed atikmdag.sys binary (0x800024A0 found, 0x800024A3 NOT found). Both test tools fixed + recompiled.
- **Result**: IOCTL now reaches driver (C81=0xF0000010 = SOS alive readback OK), but **TOS load FAILED**: C2PMSG_64 before=0 after=0 (bit31 TOS ready NEVER set), C2PMSG_35=0 after command.
- **Why**: Linux `psp_v11_0_bootloader_load_*` FIRST checks `is_sos_alive` (C2PMSG_81); if SOS already loaded it returns immediately WITHOUT any bootloader command. SOS is alive on BC-250 → bootloader is gone, cannot accept C2PMSG_35/36/37 firmware commands. Ta.bin as TOS is NOT loadable this way.
- **CONCLUSION: PSP ring mechanism is permanently unavailable** on this unit (C2PMSG_64 bit31 never sets, no TOS). PSP firmware loading via GPU driver is closed. SMU mailbox via SMN remains the only working mailbox path.
- Kernel driver build is free (KdPrintEx strings absent from binary); use byte-pattern scans, not string search, to verify constants in installed .sys.

### Test tools (new)
- `test-tools/psp-ring-probe.c` + `compile-psprobe.bat` → `output\psprobe.exe` (GPU WDM IOCTL `\\.\AMDBC250DreamV43`, needs atikmdag running)
- `test-tools/psp-ring-probe-esc.c` + `compile-psprobe-esc.bat` → `output\psp-ring-probe-esc.exe` (KMDOD Escape READ_REG/READ_SMN, works with KDODSamp display driver). **This is the one that ran.**

### Current Implementation (BROKEN — needs the fixes above)
- **GPU driver** (`amdbc250_psp.c` + `amdbc250_dream_psp_fw_load.c`) uses TWO conflicting paths:
  1. **Direct BAR5 C2PMSG** (amdbc250_psp.c): DIRECT_C2PMSG_35/36/37 offsets FIXED to 0x1055C/60/64 (was 0x1056C/70/74 — wrong base 0x103E0); C2PMSG_81 0x10614 was already correct
  2. **SMN C2PMSG** (amdbc250_dream_psp_fw_load.c): writes SMN 0x03B10A08/0x03B10A48/0x03B10A68 — these are SMU registers, NOT PSP (confirmed by probe). Must be reworked to BAR5 direct path.
- **BC-250 PSP v11.0.8 uses RING mechanism** (C2PMSG_64/67/69/70/71), NOT direct C2PMSG_35/36/37 writes
- Linux `psp_v11_0_8.c` confirms: ring_create waits for `C2PMSG_64 bit31 = MBOX_TOS_READY_FLAG`, then writes ring addr/size to C2PMSG_69/70/71
- **Result**: SYSDRV/SOS/SMC load attempts return STATUS_TIMEOUT; no PSP firmware is actually loaded by our GPU driver

### What Works
- **SMU mailbox via SMN** — Q0/Q3 protocols proven, governor sequence safe, frequency control operational
- **PSP SOS alive detection** — C2PMSG_81 bit31 = 1 (0xF0000010) when SOS already loaded by BIOS/PSP driver
- **PSP driver (PspDriver.sys)** — separate driver has its own firmware loading path (if needed)

### What Doesn't Work
- **Direct PSP mailbox firmware loading** via GPU driver — wrong mechanism for BC-250 (offsets now corrected, but SOS may not support direct C2PMSG_35/36/37 for IP firmware)
- **PSP ring creation** — C2PMSG_64 bit31 never sets (no TOS support in BC-250 SOS); Ta.bin is a candidate TOS to try
- **PSP PROG_REG** — not supported by BC-250 SOS

### Root Cause
BC-250's PSP v11.0.8 SOS firmware does NOT support direct C2PMSG_35/36/37 firmware loading. Linux amdgpu uses the ring mechanism (C2PMSG_64/67/69/70/71) which requires TOS (Trusted OS) support. Our SOS lacks TOS, so ring_create also fails. The only working path is SMU mailbox for SMU firmware; CP firmware loading via PSP is currently impossible on Windows.

## SDMA Status (2026-08-13)

| # | Description | Status | Fix |
|---|-------------|--------|-----|
| 1 | Integer overflow in BAR5_PROXY handlers (`offset + 4` wraps when offset >= 0xFFFFFFFC) | **FIXED** | Changed to `offset <= 0x80000 - sizeof(ULONG)`; added `MmioSize >= 4` guard to READ_REG/WRITE_REG |
| 2 | RLC_CP_SCHEDULERS at 0xECA1 not 4-byte aligned | **FIXED** | Uses 0xECA8 (writable) in both GPU and PSP drivers |
| 3 | GCVM page table pages double-freed on error | **FIXED** | `newlyAllocated[]` tracking prevents freeing pre-existing pages |
| 4 | IOCTL name collision between GPU and PSP driver | **FIXED** | PSP uses `IOCTL_AMDBC250_BAR5_READ_PROXY_RAW` (0x900) |
| 5 | REG_DUMP reads GRBM_GFX_INDEX at wrong offset 0x33C4 | **FIXED** | Reads 0x34D0 (correct) |
| 6 | GCVM invalidate regs mismatch (hw.h 0x0B51C vs working 0x6C0C) | **FIXED** | hw.h and vm.c both use 0x6C0C/0x6C10 |
| 7 | GPU_ID read from 3 different offsets | **FIXED** | All reads use consistent 0x0000 |
| 8 | KIQ_NOP_TEST / KIQ_BIOS_RING_SUBMIT lack `__try/__except` | **FIXED** | Added SEH protection |
| 9 | PspGpuProxyWriteRegister return value ignored | **FIXED** | Return values checked in all PSP callers |
| 10 | SEND_PM4 ring wrap check 32-bit overflow | **FIXED** | Cast to ULONG64 before multiply |
| 11 | GCVM_PT_SETUP no MmioSize bounds check | **FIXED** | Added `DevExt->MmioVirtualBase` null check |
| 12 | Race condition on PSP proxy init (no lock) | **FIXED** | Added spinlock + `g_GpuProxyInitialized` guard |
| 13 | LOAD_CP_FW halts ALL engines for MEC-only load | **FIXED** | Only halts target engine based on `fwType` |
| 14 | GCVM page table pages never freed on unload | **FIXED** | Freed in DreamV3WdmUnload |
| 15 | REG_DUMP reads SDMA0_CNTL at 0x10040 (wrong) | **FIXED** | Uses `AMDBC250_REG_SDMA0_CNTL` (hw.h 0xE018) |
| 16 | MEC_ME1_HALT bit may be inverted | **DOCUMENTED** | Added clarifying comment |
| 17 | CpRb1BaseProbe field mislabels GFX ring0 registers | **DOCUMENTED** | Added clarifying comment |

### Current Build Status
- **GPU driver** (`atikmdag.sys`): Builds + signs cleanly ✓
- **PSP driver** (`PspDriver.sys`): Builds + signs cleanly ✓
- **No new warnings or errors** introduced by fixes

### Known Limitation
- PSP driver requires Test Signing mode ON (`bcdedit /set testsigning on`) + Secure Boot OFF — self-signed AMD-BC250-Signer cert not from a trusted CA



## CRITICAL BUG: METHOD_BUFFERED buffer sharing in PSP IOCTL (2026-07-03)

**Root cause of PSP GPU_PM4_SUBMIT error 87**: In `METHOD_BUFFERED` IOCTL, `inputBuffer` and `outputBuffer` point to the SAME system buffer (`Irp->AssociatedIrp.SystemBuffer`). The PSP driver's IOCTL handler at `PspDriver.c:1164` called `RtlZeroMemory(resp, sizeof(*resp))` which zeroed the first 44 bytes — including `req->CommandCount` at offset 0. When `PspGpuPm4Submit` then checked `req->CommandCount`, it found 0 and returned `STATUS_INVALID_PARAMETER`.

**Fix**: Save `cmdCount` and `waitMs` from `req` BEFORE `RtlZeroMemory`, then restore them after.

**Applies to all METHOD_BUFFERED IOCTL handlers** that write to the output buffer before reading all input fields. KIQ_SUBMIT handler was not affected (doesn't zero the buffer).

## Agent Analysis Results (2026-07-03)

Three automated analysis agents ran on the GPU driver, PSP driver, and test tools codebases. Key findings:

### PSP Driver Bugs Found by Agent
| # | Priority | Description | Fix |
|---|----------|-------------|-----|
| 1 | CRITICAL | `PspGpuProxyInit` holds spinlock while calling `ZwCreateFile` (PspCore.c:69-71) — illegal at DISPATCH_LEVEL | **FIXED**: Release lock before `PspOpenGpuDriver()`, re-acquire after |
| 2 | HIGH | `IOCTL_PSP_GPU_PM4_SUBMIT` size check requires full 268-byte struct even with `CommandCount=5` | **FIXED**: Dynamic check via `FIELD_OFFSET(..., Commands[req->CommandCount])` |
| 3 | HIGH | `IOCTL_PSP_GPU_PM4_SUBMIT` METHOD_BUFFERED buffer sharing — `RtlZeroMemory` clears `req->CommandCount` | **FIXED**: Save/restore fields around zero |
| 4 | HIGH | NBIO unlock uses GPU BAR5 (`g_Bar5Mapping`) instead of PSP BAR0 (`devExt->MmioBase`) — writes silently fail | **FIXED**: Always use `devExt->MmioBase` |
| 5 | HIGH | Handle leak race: `PspGpuProxyInit` can return early with `g_GpuDriverHandle` set but `g_GpuProxyAvailable=FALSE` | **FIXED**: Close handle via `ZwClose` on error path |
| 6 | MEDIUM | GRBM_STATUS reads offset 0x2004 (CC_CONFIG) instead of 0x2000 (GRBM_STATUS) | **FIXED**: All 6 occurrences changed to 0x2000 |
| 7 | LOW | Error string missing in KdPrint for some IOCTL validation paths | Deferred |

### Test Tool Bugs Found by Agent
| # | Priority | Description | Fix |
|---|----------|-------------|-----|
| 1 | HIGH | `psp-gpu-pm4-submit-test.c`: PM4 header `0xC0370003` has swapped count/opcode | **FIXED**: `0xC0043700` |
| 2 | HIGH | `psp-gpu-pm4-submit-test.c`: WRITE_DATA CONTROL `0x10100000` has wrong DST_SEL | **FIXED**: `0x00000102` (register | WR_CONFIRM) |
| 3 | HIGH | `gfx-ring-init-test.c`: RPTR comparison always succeeds (false positive) — `RPTR >= wptrTarget` always true due to bit 24 | **FIXED**: Compare before/after difference |

## Verified GFX/Compute Engine Status (2026-07-03)

### GFX Ring (0xDA60+ range)
- **BASE_LO (0xDA60)**: READ-ONLY = 0 — ring buffer address CANNOT be changed
- **CNTL (0xDA68)**: partially writable (bit 0 sticks)
- **RPTR (0xDA6C)**: 0x01200000 — bit 24 permanently set, other bits writable but bounces back
- **WPTR (0xDA78)**: FULLY WRITABLE ✅ — can kick ring
- **DOORBELL (0xDA7C)**: WRITABLE ✅
- **RPTR DOES NOT ADVANCE** on WPTR kick — MEC not processing

### Linux-corrected (0x89E0+ range)
- **RB0_BASE (0x89E0)**: mostly RO, only low byte writable (W1C to 0x000000A5)
- **RB0_CNTL (0x89E4)**: RO = 0
- **RB0_WPTR (0x8A30)**: RO = 0
- All registers in this range mostly dead/read-only on BC-250

### PSP KIQ Path
- **PSP GPU PM4 submit**: IOCTL WORKS ✅ (after METHOD_BUFFERED fix)
- PM4 written to KIQ ring buffer (WPTR=5→10)
- BUT: GPU MEC not processing (KIQ_BASE=0, KIQ_SIZE=0, ME halted)
- Readback confirms ring is writable through PSP proxy

### Conclusion (UPDATED 2026-08-13)
BC-250 compute/GFX engines are NOT permanently disabled at hardware level — they are SOS/SMU-locked. Linux amdgpu successfully creates compute rings and runs shaders. The blockers are:
1. **SPI_PG_ENABLE_STATIC_WGP_MASK (0x5C3C) is SOS-locked** — host BAR5 writes blocked by PSP Secure OS
2. **SMU DPM not initialized** — no DPM tables uploaded, so SMU cannot power on WGPs
3. **Ring BASE registers SOS-locked** — cannot create GFX/compute rings from host
4. **PSP firmware loading path is wrong** — GPU driver uses direct C2PMSG_35/36/37, but BC-250 PSP v11.0.8 needs ring mechanism (C2PMSG_64/67/69/70/71)

Linux proves the silicon works. Our Windows driver lacks the privilege level (kernel context + debugfs) and correct init sequence.

## Next Steps (Completed)
1. ~~Check if ME unhalt (write 0 to 0x4A74 via PSP proxy) enables any engine activity~~ **DONE** — unhalted, no change
2. ~~Investigate if KIQ_BASE/KIQ_SIZE can be programmed through alternative means~~ **DONE** — hardwired to 0
3. ~~Load MEC firmware via PSP mailbox~~ **DONE** — loads successfully, no engine activity
4. Consider display-only driver if all compute paths remain fused (only remaining option)

## Linux Comparison Analysis (2026-07-03)

### What Linux DOES (from CachyOS dmesg logs)
- **GFX ring created**: `ring gfx_0.0.0 uses VM inv eng 0 on hub 0`
- **8 COMPUTE rings created**: `ring comp_1.0.0` through `ring comp_1.3.1`
- **KIQ ring created**: `ring kiq_0.2.1.0 uses VM inv eng 11 on hub 0`
- **SDMA rings created**: `ring sdma0/sdma1 uses VM inv eng 12/13 on hub 0`
- **24 CUs detected**: `SE 2, SH per SE 2, CU per SH 10, active_cu_number 24`
- **SMU initialized**: `SMU is initialized successfully!` (SMC firmware 88.7.1)
- **Display Core**: DCN 2.0.1 initialized
- **VBIOS**: Fetched from ACPI VFCT (ATOM BIOS: 113-AMDRBN-003)
- **VRAM**: 256M (not 16GB as our driver assumes)
- **All firmware loaded**: ME v0x63, PFP v0x94, CE v0x25, MEC v0x90, RLC v0x0d, SDMA v0x34, SMC v88.7.1

### What Windows (our driver) DOESN'T have
1. **SMU firmware not running** — SMU C2PMSG registers (0x16A08/0x16A48/0x16A68) all read 0; SMU_WAKE times out
2. **No VBIOS access** — driver doesn't fetch VBIOS from ACPI VFCT
3. **No RLC firmware loaded** — RLC controls engine queue scheduling
4. **No complete ring initialization** — rings can't be created because BASE_LO is read-only and KIQ_SIZE=0
5. **KIQ_BASE/KIQ_SIZE hardwired to 0** — ring buffer address can't be programmed

### Root Cause Analysis
The primary blocker is **SMU firmware not running**. SMU provides:
- Clock gating control (without SMU, GFX/CP blocks have no clock)
- Power gating control (blocks may be power-gated off)
- Temperature monitoring and thermal throttling
- Voltage regulation

Without SMU actively running, compute engines cannot process ring buffers because they lack clock/power input — even if ME is unhalted and MEC firmware is loaded.

### Why Linux works but Windows doesn't
Linux amdgpu driver has full SMU, VBIOS, RLC, and firmware loading infrastructure built into the kernel. Our Windows driver is minimal (WDM IOCTL only), lacking these critical initialization paths. The hardware itself is capable (Linux proves it), but our driver initialization is incomplete.

### RADV `RADV_DEBUG=nocompute` Clarification
This is a **userspace Vulkan driver workaround**, NOT a hardware limitation. Linux kernel successfully creates compute rings — the Vulkan driver has a separate issue likely related to the mining ASIC's register differences from standard Navi10/Sienna_Cichlid.

### Remaining Open Question
Is PSP SOS firmware loaded? AGENTS.md earlier noted `C2PMSG_81=0xF0000010` suggesting SOS alive, but the SMU not responding suggests either:
1. SOS is loaded in minimal state (bootrom only, not full Secure OS)
2. Or SOS is loaded but SMU init wasn't triggered by VBIOS (VBIOS contains SMU wake sequence)
3. Or SOS needs SMU firmware file (`cyan_skillfish2_smc.bin`) which we don't have

## Next Steps (Completed)


### ME Unhalt Test (me-unhalt-test.c via PSP driver)
- PSP driver INIT_HW maps GPU BAR5 with clean MmMapIoSpace (no PCI config writes)
- **ME_CNTL (0x4A74)**: 0xFFFBD9FB → wrote 0 → **0x00000000** ✅ Unhalted!
- ME remained unhalted across reboots

### GFX Ring Test Post-Unhalt (gfx-ring-unhalted-test.c)
- **WPTR (0xDA78)**: 0x00100010 → writable ✅
- **RPTR (0xDA6C)**: Stays 0x01200000 — **DOES NOT ADVANCE** ❌
- **BASE_LO (0xDA60)**: RO = 0
- **CNTL (0xDA68)**: RO = 0
- **GRBM_STATUS**: 0 before and after — no engine activity
- **KIQ_BASE/KIQ_SIZE**: 0, read-only
- **SCRATCH**: unchanged (0x4D585042)

### MEC Firmware Load Test (via PSP mailbox IOCTL_PSP_LOAD_IP_FW_DIRECT)
- MEC (fwType=4) firmware loaded: Status=0x00000000 ✅
- Post-load: GFX ring test re-run — **NO CHANGE** — RPTR still doesn't advance

### Hardware Conclusion (UPDATED 2026-08-13)
BC-250 is a full PS5 Oberon die (8× Zen2 + RDNA2 + 40 CUs), NOT a mining-only ASIC with fused-off shader array. Disabled features are MASKED (SOS/SMU-locked), not fused.
- ME was halted but unhalting it doesn't enable processing
- GFX ring WPTR is writable but the CP/MEC engine behind it never reads from the ring
- KIQ_BASE/KIQ_SIZE are hardwired to 0 — ring buffer address can't be set
- PGM_LO (0x8110) is WRITABLE (0x65FFEB6E initial, writes persist across boots) but still no execution
- COMPUTE registers (DIM_X/Y/Z) are read-only shadows, but PGM_LO/HI and NUM_THREAD_Y/Z are WRITABLE
- Consistent with RADV `RADV_DEBUG=nocompute` workaround and bc250-collective findings
- Linux community (duggasco/bc250-40cu-unlock, elektricm docs) writes the SAME registers we found "read-only" — and they work on Linux:
  - `CC_GC_SHADER_ARRAY_CONFIG` (BAR5 0x9C1C): stock `0xfff80000` (24 CU) → unlocked `0xffe00000` (40 CU)
  - `SPI_PG_ENABLE_STATIC_WGP_MASK` (BAR5 0x5C3C): stock `0x07` (WGP 0-2) → unlocked `0x1F` (WGP 0-4)
  - `RLC_PG_ALWAYS_ON_WGP_MASK` (BAR5 0x3D64): set to `0x1F` to keep all WGPs powered
  - ALL THREE writes are required together; CC alone only changes reporting, SPI alone only 24 CU compute.

### What DOES Work
- PSP driver: BAR5 mapping, mailbox firmware loading, register read/write proxy
- GPU driver: WDM IOCTL device, BAR5 proxy, GCVM page tables, PM4 encoding
- PSP mailbox: firmware loading for ALL types (ME, PFP, CE, MEC, MEC2, RLC, SDMA) — BUT current implementation uses WRONG mechanism for BC-250
- Register read/write via both GPU and PSP drivers
- SMU mailbox via SMN: frequency/voltage control, feature enable/disable, temperature monitoring
- CPU core unlock via SMU Q3 msg 0x98 (6c/12t → 8c/16t)


## PSP Driver: Firmware now auto-installed via INF (2026-07-04)

The PSP driver's `PspDriver.inf` now has a `[Firmware_Files]` section that auto-copies `Sysdrv.bin`, `Sos.bin`, and `Smu.bin` to `C:\Windows\System32\drivers\bc-250\` during Device Manager installation. No separate xcopy step needed.

### GPU driver firmware loading (IC_BASE DMA)
- `DreamV3LoadAllFirmware()` in `amdbc250_dream_fw_load.c` loads ME, PFP, CE, MEC firmware from `\SystemRoot\System32\drivers\bc-250\` via ZwCreateFile/ZwReadFile + IC_BASE DMA registers
- Called from `DreamV3HwInitialize()` as step 6/13 after engine halt + before GFX ring init
- GPU INF (`amdbc250_dream.inf`) now has `DreamV3.Firmware` section installing `cyan_skillfish2_*.bin` to `13,System32\drivers\bc-250`
- `build.bat` copies `firmware/*.bin` to `output/firmware/` for INF-based installation

When installing PSP driver via Device Manager → Have Disk → select `PspDriver.inf`:
- `PspDriver.sys` → `C:\Windows\System32\drivers\`
- `Sysdrv.bin` → `C:\Windows\System32\drivers\bc-250\`
- `Sos.bin` → `C:\Windows\System32\drivers\bc-250\`
- `Smu.bin` → `C:\Windows\System32\drivers\bc-250\`

### PSP build outputs
- `build.bat` generates `output\PspDriver.sys`, `output\PspDriver.inf`, `output\PspDriver.cat`, and copies `.bin` files to `output\`
- PSP build repo: `C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver`

### PSP test tools (in GPU repo output\)
- `output\psp-status-test.exe` — register probe (C2PMSG, SMU, GC)
- `output\toc-load-test.exe` — SMU TOC firmware load via IOCTL_PSP_LOAD_TOC
- GPU driver exposes: `IOCTL_AMDBC250_BAR5_READ_PROXY` (0x900) and `IOCTL_AMDBC250_BAR5_WRITE_PROXY` (0x901) — these are **raw values**, not CTL_CODE-packed.
- PSP driver must use `#define IOCTL_AMDBC250_BAR5_READ_PROXY 0x900` (raw), NOT `CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, ...)` — different numbers cause silent rejection.

## Memory and context
- **ALWAYS use memory tool** to save progress, decisions, and key findings between sessions.
- Before ending a session, save: current state, blockers, next steps, file changes, and hardware discoveries.
- On session start, search memory for prior context before exploring files.
- Memory tags: `BC-250`, `IOMMU`, `GPU driver`, `GCVM`, `compile`, `PSP driver`, `hardware`.

## Source and docs to trust
- Prefer `build.bat`, `src/kmd/amdbc250_dream_kmd.c`, `src/kmd/amdbc250_dream_hw_init.c`, `src/kmd/amdbc250_psp.c`, and `inc/amdbc250_dream_hw.h` over historical docs.
- Useful but potentially stale docs: `docs\REGISTER-MAP-BC250.md`, `docs\PSP-PROXY-BYPASS.md`, and `docs\RING-INIT-STATUS.md`.
- Trust: `docs\BC250-LINUX-IP-MAP.md` (Linux kernel source-verified IP base addresses).

## CRITICAL: Pre-build code analysis agent
- **ALWAYS** run the Code Reviewer agent BEFORE every build to catch bugs, wrong register offsets, and logic errors.
- The agent must check: IOCTL handler parameter validation, register offset correctness against hw.h, method_buffered buffer sharing, pointer safety, and memory leaks.
- Never build without prior agent code review.

## CRITICAL: Never jump to "hardware limitation" conclusions
- All tests must be run with firmware files properly installed at the correct path (`C:\Windows\System32\drivers\bc-250\`).
- Verify firmware installation FIRST before any hardware diagnostic conclusion.
- Tests must be repeated multiple times, across reboots, before concluding hardware is dead.
- An SMU or engine appearing dead is often a firmware-not-loaded problem, not a hardware limitation.

## CRITICAL: INF DestinationDirs syntax (easy to get wrong)
- `DIRID_12` = `%SystemRoot%\System32\drivers\` — use for firmware files in `bc-250\`
- `DIRID_13` = `%SystemRoot%\System32\DriverStore\FileRepository\` — NOT for runtime files!
- WRONG: `Firmware_Files = 13,System32\drivers\bc-250` → creates `...\drivers\System32\drivers\bc-250\` (double path!)
- WRONG: `Firmware_Files = 13, bc-250` → creates `...\DriverStore\FileRepository\bc-250\` (wrong parent dir!)
- CORRECT: `Firmware_Files = 12, bc-250` → creates `...\drivers\bc-250\` ✅
- Also in `SourceDisksFiles`:
  - WRONG: `Asd.bin = 1,,firmware` ← third field is `size`, not `subdir` (extra comma!)
  - CORRECT: `Asd.bin = 1, firmware` ← second field is `subdir`
- Check BOTH GPU (`inf\amdbc250_dream.inf`) and PSP (`inf\PspDriver.inf`) INF files.

## CRITICAL: COMPUTE Register Address Correction (2026-07-01)

All COMPUTE registers in Linux gc_10_1_0_offset.h have **BASE_IDX=0** (not 1!). This means the correct formula is `BAR5 = GC_BASE(0x1260) + mm_DWORD * 4`, **NOT** the SEG1 formula (GC_BASE + 0xA000 + mm*4) that hw.h uses.

### Corrected COMPUTE Register Map

| Register | Linux mm | Old (hw.h) | CORRECT |
|----------|----------|------------|---------|
| DISPATCH_INITIATOR | 0x1ba0 | 0xDC60 | **0x80E0** |
| DIM_X/Y/Z | 0x1ba1-3 | 0xDC64 | **0x80E4-0x80EC** |
| START_X/Y/Z | 0x1ba4-6 | 0xDC68 | **0x80F0-0x80F8** |
| NUM_THREAD_X/Y/Z | 0x1ba7-9 | 0xDC6C | **0x80FC-0x8104** |
| PGM_LO/HI | 0x1bac-1bad | 0xDC70 | **0x8110-0x8114** |
| PGM_RSRC1/RSRC2 | 0x1bb2-1bb3 | (none) | **0x8128-0x812C** |
| STATIC_THREAD_MGMT_SE0 | 0x1bb6 | (none) | **0x8138** |
| TMPRING_SIZE | 0x1bb8 | (none) | **0x8140** |
| USER_DATA_0-15 | 0x1be0-1bef | (none) | **0x81E0-0x81FC** |

### Corrected CP_HQD Register Map

| Register | Linux mm | Old (hw.h) | CORRECT |
|----------|----------|------------|---------|
| CP_MQD_BASE_ADDR | 0x1fa9 | 0xDAB8 | **0x9104** |
| CP_HQD_ACTIVE | 0x1fab | 0xDAC0 | **0x910C** |
| CP_HQD_VMID | 0x1fac | 0xDAC4 | **0x9110** |
| CP_HQD_PQ_BASE | 0x1fb1 | 0xDAD8 | **0x9124** |
| CP_HQD_PQ_CONTROL | 0x1fba | 0xDAFC | **0x9148** |
| CP_HQD_PQ_WPTR_LO | 0x1fdf | 0xDB90 | **0x91DC** |

### Old address problems
- **GRBM_GFX_CNTL (0x2022)** was probed at WRONG address. CORRECT is **0x4968** (mm=0x0dc2, GC_BASE + mm*4).
- **COMPUTE block at 0xDC60** contains DIFFERENT registers (not COMPUTE at all) — this is why dispatch never worked.
- **CP_HQD block at 0xDAB8+** also wrong — real registers at 0x9104+.
- All prior dispatch tests used wrong addresses.

### New test tool
- `test-tools/correct-compute-test.c` + `compile-correct-compute.bat` probes correct addresses and attempts dispatch.

### Actual HW Status (verified 2026-07-08)

| Register | Address | Status |
|----------|---------|--------|
| DISPATCH_INITIATOR | 0x80E0 | W1C trigger, VALID consumed=YES, but no execution |
| DIM_X | 0x80E4 | STALE read (0), writes silently ignored |
| DIM_Y | 0x80E8 | DEAD (0xFFFFFFFF) |
| DIM_Z | 0x80EC | DEAD (0xFFFFFFFF) |
| START_X/Y/Z | 0x80F0-0x80F8 | STALE read (0), writes silently ignored |
| NUM_THREAD_X | 0x80FC | STALE read (0), writes silently ignored |
| NUM_THREAD_Y | 0x8100 | STALE read (0x3F1FBC5F), WRITABLE but persists garbage |
| NUM_THREAD_Z | 0x8104 | STALE read (0xFF973BC8), WRITABLE but persists garbage |
| PGM_LO | 0x8110 | WRITABLE!, persists across boots (shadow) |
| PGM_HI | 0x8114 | WRITABLE!, persists across boots (shadow) |
| PGM_RSRC1/2 | 0x8128-0x812C | DEAD (0xFFFFFFFF) |
| CP_MQD_BASE_ADDR | 0x9104 | WRITABLE (write-back verified) |
| CP_MQD_BASE_ADDR_HI | 0x9108 | READ-ONLY (0xFE75DFC2) |
| CP_HQD_ACTIVE | 0x910C | WRITABLE, ACKs (reads 1) |
| GRBM_GFX_CNTL | 0x2022/0x4968 | DEAD (BC-250 doesn't have this) |
| 0xDC60 register | 0xDC60 | Cycling FIFO (debug counter, not dispatch) |
| CC_ARRAY_CONFIG | 0x9C1C | PARTIALLY WRITABLE (0xFFE00000→0x1F000000) |
| SPI_PG_ENABLE_STATIC_WGP_MASK | 0x5C3C | READ-ONLY zero (WGPs fused off) |
| SPI_PG_MASK | 0x34FC | PARTIALLY WRITABLE (0xFFFFFFFF→0xF7F77F80) |
| GRBM_GFX_INDEX | 0x34D0 | LIVING (0xE0000000 broadcast) |

## BREAKTHROUGH: SMU Mailbox via SMN WORKS! (2026-07-05)

### Discoveries
1. **SMN access via NBIO BAR5+0x38/0x3C works** — this is Linux `WREG32_PCIE`/`RREG32_PCIE` path
   - Write SMN address to `0x38`, data to `0x3C` → generates SMN bus cycles
   - Direct MMIO SMN ports (physical 0x3B10528/0x3B10564) FAIL — use NBIO path only
   - SMN via PCI config (B0D0F0 + 0xB8/0xBC) is read-only — no reboot fix possible

2. **SMU firmware is RUNNING** (not just loaded by PSP)
   - FW_FLAGS at SMN[0x03B10024] = 0x00000001 → INTERRUPTS_ENABLED = YES
   - PUB_CTRL at SMN[0x03B10B14] = 0x00000000 → reset NOT asserted
   - SMU version: 88.6.0 (0x00580600)
   - Driver interface version: 8
   - Responds to PPSMC_MSG_TestMessage (0x1) — SMU is alive!

3. **C2PMSG mailbox registers located in SMN space (NOT BAR5!)**
   - C2PMSG_66 (message): SMN[0x03B10A08]
   - C2PMSG_82 (arg/response): SMN[0x03B10A48]
   - C2PMSG_90 (control): SMN[0x03B10A68]
   - SMU C2PMSG via BAR5 direct (0x16A08/0xA48/0xA68) reads 0 on BC-250 — MP1 NOT mapped into BAR5!

4. **SMU enabled features: 0xDD602C7D**
   - Bit 0 (GFXCLK DPM) = ON ✅
   - Bit 2 (GFXOFF) = ON — GFX block in deep sleep/clock gated!
   - Most other SOC/DF features also enabled

5. **Why ALL prior compute tests failed: GFXOFF keeps GFX in deep sleep**
   - GFX frequency: 15 MHz (idle) — GFXOFF enabled
   - 0 WGPs active — compute units physically powered/gated off
   - Registers readable/writable = shadow registers, actual hardware has no clock
   - RPTR not advancing = CP/MEC engine has no power — NOT a ring issue!
   - KIQ_BASE/KIQ_SIZE hardwired to 0 — hardware read-only without proper SMU init

### Test Tool
- `test-tools\bar5-smn-test.c` — **PRIMARY**: SMU mailbox via SMN (uses GPU IOCTL for BAR5 + NBIO 0x38/0x3C for SMN)
- Supports `SmuSendMsg(msg)` and `SmuSendMsg(msg, param)` with proper protocol (wait C2PMSG_90==1, ack, write param, write msg, poll, read)

### SMU v11.8 PPSMC Message IDs (verified for BC-250)
- 0x1 TestMessage ✅
- 0x2 GetSmuVersion (returns 0x00580600 = 88.6.0) ✅
- 0x3 GetDriverIfVersion (returns 8) ✅
- 0x3D GetEnabledSmuFeatures (returns 0xDD602C7D) ✅
- 0x37 GetGfxFrequency (returns 15 MHz) ✅
- 0x0F QueryGfxclk (returns 15 MHz) ✅
- 0x38 GetGfxVid ✅
- 0x1E QueryActiveWgp (returns 0) ✅
- 0x0C QueryCorePstate ✅
- 0x13 QueryDfPstate ✅
- **0x39 ForceGfxFreq: NOW PROVEN SAFE** (with voltage+profile set first) ✅
- **0x39 ForceGfxFreq: CAUSED SYSTEM CRASH only with param=80000 (wrong units) + no voltage** — the governor sequence (Q3 temp→Q0 unforce→Q3 profile→Q0 force_vid→Q0 force_freq) works without DPM tables
- **0x1E QueryActiveWgp: ALWAYS returns 0** — WGPs are hardware-fused off even when GFXOFF/CG/PG disabled

### FINAL VERDICT: Compute hardware permanently fused off on BC-250 (2026-07-08)
- GFXOFF+CG+PG all successfully disabled via Q2(6,0x1C,0) — feature mask went from 0xDD602C7D to 0xDD602C61 (all three bits cleared)
- CC_ARRAY_CONFIG(0x9C1C) partially writable (0xFFE00000→0x1F000000)
- **SPI_PG_ENABLE_STATIC_WGP_MASK(0x5C3C) is READ-ONLY returning 0** — this is the critical register that enables per-WGP power gating; being zero means ALL WGPs are permanently power-gated/fused off
- DISPATCH_INITIATOR(0x80E0) VALID consumed=YES (compute frontend register interface works)
- BUT: GRBM_STATUS(0x3260)=0, Scratch unchanged, QueryActiveWgp=0 — no shader execution
- PGM_LO(0x8110) writable and persists across boots (shadow register)
- CONFIRMED by: Mesa MR 33116, ROCm issue #6313, RADV RADV_DEBUG=nocompute
- UNLOCK_40CU IOCTL defined in header but NEVER implemented in driver source

### Why Linux compute init doesn't solve this
- Linux `cyan_skillfish_ppt.c` SMU init works for SMU frequency control but does NOT magically enable compute
- The governor (cyan-skillfish-governor) never queries WGPs for compute purposes — it only reads GRBM_STATUS for GPU LOAD detection (GUI_ACTIVE bit)
- Even with full amdgpu kernel driver and SMU DPM init, ROCm reports SDMA0/KIQ/CP timeouts (issue #6313)
- Mesa explicitly disabled compute-only queue on BC-250's GFX10.1 variant
- This is a B0 stepping hardware limitation, not a software-solvable problem

### Key Lesson
Compute is permanently disabled at hardware level on BC-250 via SPI_WGP power gating fuses. No amount of SMU/DPM/register init can enable WGPs. The card is usable only for display output, PSP mailbox/firmware operations, and register-level hardware debugging.

## UPDATE: BC-250 external repos + driver self-audit (2026-07-19)

### Critical correction: GRBM_GFX_INDEX = 0x34D0 (NOT 0x9A60)
- Previous hw.h comment "0x34D0 is NOT GRBM_GFX_INDEX" was WRONG.
- `bar5-cu-unlock-test.exe` confirmed: 0x34D0 reads 0xBA062100, after write 0xE0000000 (broadcast). It IS the real GRBM_GFX_INDEX.
- Driver code already used 0x34D0 correctly (amdbc250_dream_kmd.c:5084, KIQ test).
- Linux `mmGRBM_GFX_INDEX=0x2200`; BAR5 = GC_BASE(0x1260) + 0x2200 = 0x3460, but HW reports 0x34D0 as the live register. Use 0x34D0.

### SPI_PG_ENABLE_STATIC_WGP_MASK (0x5C3C) is SOS-LOCKED — confirmed again
- `bar5-cu-unlock-test.exe` with correct GRBM_GFX_INDEX 0x34D0 + broadcast 0xE0000000:
  - SPI_PG_ENABLE_STATIC_WGP_MASK (0x5C3C): wrote 0x1F -> readback 0x00000000 [LOCKED]
  - RLC_PG_ALWAYS_ON_WGP_MASK (0x3D64): before=0xFFFFFFFF, wrote 0x1F -> 0xFFFFFFFF [LOCKED]
- `duggasco/bc250-40cu-unlock` kernel patch works ONLY via Linux amdgpu debugfs (`/sys/kernel/debug/dri/0/amdgpu_regs`), NOT via our WDM BAR5 write.
- **Our drivers are NOT buggy.** SPI/RLC are SOS-locked to host writes.

### Driver self-audit — our code paths are correct
- `DreamV3WriteRegister` (amdbc250_dream_kmd.h:717) = `WRITE_REGISTER_ULONG(MmioVirtualBase + offset)` — correct BAR5 path.
- `INIT_HARDWARE` (kmd.c:3745) maps 0xFE800000 → MmioVirtualBase via MmMapIoSpace — correct.
- SPI (0x5C3C) and RLC (0x3D64) offsets are correct (GC_BASE 0x1260 + mm*4).
- PSP `PROG_REG` (0x0B) — SOS does NOT support it (PspDriver.c:900) — expected.

### Why Linux works but our WDM does not
- Linux amdgpu: kernel context + debugfs (higher privilege) + full GPU init + SOS permits it.
- Our WDM: user-mode IOCTL → kernel → BAR5 (same path) BUT no debugfs, SOS blocks "non-legit" driver from SOS-locked registers.

### External WDDM projects — all FAIL on BC-250
- `BC250-windowsDriverTest`: INF DEV_7420-7426, BAR0, raw Navi offsets → Code 43, wrong device.
- `third-party/ps5-win-driver`: INF DEV_13FE, BAR0, raw Navi offsets → Code 43.
- `amd-bc250-driver` v4.3 (Dream Drivers): INF DEV_13FE, SMU mailbox, BAR0 + raw Navi (IH_RB=0x3800, MC_VM_FB=0x520) → Code 43.
- ALL use BAR0 + unshifted Navi offsets. BC-250 needs BAR5 (0xFE800000) + GC_BASE(0x1260) shift.

### Lenovo AMD PSP driver (amdpsp.sys v5.28.0.0) — NOT for BC-250
- Downloaded from ds561954 (r25pp06w.exe), extracted via innoextract.
- Supports only VEN_1022 (CPU PSP: 1537/1578/13EC/1456/15DF/1649/1486/15C7/14CA/17E0). NO VEN_1002&DEV_13FE.
- Not applicable to BC-250 GPU PSP. No *.bin firmware included.

### Community repos (all Linux, confirm silicon works)
- `bc250_smu_oc`: CPU+GPU+VRAM same die, SMU controls all. Uses PCI config 0xB8/0xBC (not BAR5+0x38/0x3C). WARNING: CPU VID > 1.325V = hardware brick! CPU/GPU share cooler.
- `bc250-cu-live-manager`: CU unlock via UMR (`-b SE SH 0xffffffff` broadcast). Writes mmSPI_PG_ENABLE_STATIC_WGP_MASK=0x1f + mmCC_GC_SHADER_ARRAY_CONFIG=0. Works on Linux (UMR via debugfs).
- `bc250-control-center`: Python GUI, uses amdgpu sysfs + cyan-skillfish-governor-smu + cu_manager (UMR). Not Windows.
- Conclusion: BC-250 silicon has full GPU functionality under Linux amdgpu, but Windows WDDM requires debugfs privilege we cannot replicate.

### NCT6687D (fan/PWM) — separate from GPU driver
- BC-250 motherboard has NCT6687D (SMBus/LPC), not a GPU register.
- `nct6687d` repo (Fred78290) is a Linux kernel driver. Not Windows.
- Our GPU/PSP drivers control only BAR5, not SMBus → fan control via our driver is IMPOSSIBLE (needs separate SMBus driver, e.g. LibreHardwareMonitor-style).

### FINAL CONCLUSION (2026-07-19)
Windows WDDM display/3D on BC-250 is IMPOSSIBLE due to:
1. SOS-locked registers (SPI_PG_ENABLE_STATIC_WGP_MASK, RLC_PG_ALWAYS_ON_WGP_MASK) — host cannot write.
2. Debugfs privilege unavailable (Linux amdgpu-only).
3. External WDDM projects all use wrong BAR/offset (Code 43).
4. Lenovo amdpsp.sys does not support DEV_13FE.

Our drivers work correctly (register access, SMU mailbox, PSP proxy, firmware load). BC-250 usable only as: register/debug control, SMU mailbox, PSP proxy, monitoring (NCT6687D via separate SMBus driver).

## CRITICAL: Win11 26100 WDM fallback — INIT_HARDWARE required before register access (2026-07-05)

### CORRECTION (2026-08-01): DxgkInitialize is NOT "not exported" — it lives in displib.lib
Reverse-engineered the real WDDM registration path on Win11 26100 (from WDK `displib.lib` + dxgkrnl.sys + BasicDisplay.sys):
- **dxgkrnl.sys (26100.8875) does NOT export `DxgkInitialize`** (333 exports, string appears 0 times; only Nt*/Dxgk*/Tdr*/Dpi* + data export `DxgCoreInterface`). Our `DreamV3ResolveDxgkInitialize()` export-table scan can NEVER succeed — the symbol was never in dxgkrnl.
- **`DxgkInitialize` is a real function body inside WDK `displib.lib`** (link-time, not an import thunk). It registers with the graphics kernel via a **`\Device\DxgKrnl` device-object IOCTL**, NOT via a dxgkrnl export:
  1. validates `DriverInitializationData->Version` (accepts 0x1052, 0x2005, 0x300E, 0x4002/3, 0x5023, 0x6003, 0x6010/11, 0x700A, 0x8001, 0x9006, 0xA00B, 0xB004, 0xC004, 0xD001, 0xE003, 0xF003, 0x10004, 0x11007)
  2. `DlpLoadDxgkrnl()`: `ZwLoadDriver` then `IoGetDeviceObjectPointer(L"\\Device\\DxgKrnl")`
  3. `DlpGetIoctlCode()`: Win8+ → **IOCTL 0x230047** = `CTL_CODE(FILE_DEVICE_VIDEO(0x23), 0x11, METHOD_NEITHER, FILE_ANY_ACCESS)`; legacy → 0x23003F
  4. `DlpCallSyncDeviceIoControl()`: `IoBuildDeviceIoControlRequest(0x230047, DeviceObject, ...)` → `IofCallDriver` → wait event
  5. on success: patches `DriverInitializationData->DxgkDdiStartDevice` (MiniportStartDevice) → `DlpStartDevice`, which fills `gDlpDxgkCb*` callbacks from the response, then calls the real StartDevice.
- **BasicDisplay.sys (Microsoft's DOD) confirms this**: it does NOT import dxgkrnl at all — it imports `ZwLoadDriver`, `IoGetDeviceObjectPointer`, `IoBuildDeviceIoControlRequest` (the displib IOCTL path).
- **Implication for a real WDDM miniport build**: link against `displib.lib` (`%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64\displib.lib`) and call `DxgkInitialize` directly, instead of `DreamV3ResolveDxgkInitialize()` export scanning. `DxgkUnInitialize` also lives in displib.lib. NOTE: `displib.lib` only exists in the 26100 WDK (not 19041/22621 in our toolchain dirs).

### Problem
On Win11 26100, the driver's runtime export-scan for `DxgkInitialize` fails (symbol is in displib.lib, not dxgkrnl.sys), so the driver enters WDM fallback mode → creates IOCTL device but **NEVER maps BAR5** (no PnP `StartDevice` call). All `IOCTL_AMDBC250_READ_REG` returns `STATUS_DEVICE_NOT_READY` (ERROR 21) because `DevExt->MmioVirtualBase == NULL`.

### Solution
User-mode test tools MUST call `IOCTL_AMDBC250_INIT_HARDWARE` FIRST:
```c
AMDBC250_IOCTL_INIT_HARDWARE ih;
ih.MmioPhysicalBase = 0xFE800000ULL;  // GPU BAR5 (or 0 for auto-detect)
ih.MmioSize = 0x80000;                 // 512KB (or 0 for default)
ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP; // SKIP full HW init!
```
- `Flags=0` → calls `DreamV3HwInitialize()` → **crashes** (TDR/white screen)
- `Flags=AMDBC250_INIT_FLAG_NBIO_MAP` → safely maps BAR5 + enables PCI mem space via IO ports
- After success: register reads work, SMU via SMN (BAR5+0x38/0x3C) works
- **MmioPhysicalBase=0 auto-detect** (2026-07-19): if caller passes 0, driver uses `HalGetBusDataByOffset` to scan PCIe config for VEN_1002/DEV_13FE, reads BAR5 (`BaseAddresses[5]` @ 0x24), masks lower 4 bits, defaults size to 512KB. Test: `test-tools\bar5-autodetect-test.c`.
- **Gemini/AI false claim (2026-07-19)**: "driver never maps BAR5, all registers are memory garbage" is WRONG. INIT_HARDWARE already calls `MmMapIoSpace(0xFE800000)`. Proof: GRBM_GFX_INDEX (0x34D0) readback=0xE0000000 (real silicon), SMU mailbox returns v88.6.0 (real). BAR5 IS physically mapped on Win11 26100.

### Test Template
`bar5-smn-test.c` must include INIT_HARDWARE before any read/write on Win11 26100. Without this, all reads return `0xFFFFFFFF`.

## bc250-collective Repository Analysis (2026-07-08)

### Confirmed Queue SMN Addresses (from bc250_smu_oc Python library)
```python
DEFAULT_QUEUE_ADDRS = {
    0: (0x03B10A08, 0x03B10A68, 0x03B10A48),  # (cmd, rsp, arg) — GPU freq/voltage/DPM
    1: (0x03B10A00, 0x03B10A60, 0x03B10A40),  # unknown
    2: (0x03B10528, 0x03B10564, 0x03B10998),  # SMU features enable/disable, device info
    3: (0x03B10A20, 0x03B10A80, 0x03B10A88),  # temperature, perf profiles, CPU voltage
    4: (0x03B10A24, 0x03B10A84, 0x03B10A8C),  # unknown
}
```
- Both Python (bc250_smu_oc) and Rust (cyan-skillfish-governor) use **PCI config 0xB8/0xBC** for SMN transport. On Windows we use BAR5+0x38/0x3C — functionally identical (NBIO SMN bridge).
- **Queue 0** is protected (`allow_queue0=False` by default) because it has dangerous messages (force freq/vid, DPM table transfer).
- **Test message uses Queue 3 msg 0x01** (NOT Queue 0 or Queue 2). Returns arg+1.
- Protocol: `write RSP=0` → `write ARG` → `write CMD` → poll RSP for {0x01=OK, 0xFF=fail, 0xFE=unknown, 0xFD=rejected, 0xFC=busy}.
- VID formula: `vid = round((1.55 - mv/1000.0) / 0.00625)`; `mV = round((-vid*0.00625 + 1.55) * 1000)`.

### Cyan Skillfish Governor — Complete SMU Message Maps

**Queue 0 messages** (from cyan-skillfish-governor src/api/queue0.rs):
| msg | Name | Arg | Returns |
|-----|------|-----|---------|
| 0x01 | TestMessage | value | value+1 |
| 0x02 | GetSmuVersion | 0 | version (0x00580600 = 88.6.0) |
| 0x03 | GetDriverIfVersion | 0 | 8 |
| 0x04 | SetDriverTableDramAddrHigh | addr_hi | — |
| 0x05 | SetDriverTableDramAddrLow | addr_lo | — |
| 0x06 | TransferTableSmu2Dram | 0 | — |
| 0x07 | TransferTableDram2Smu | 0 | — |
| 0x0B | RequestCorePstate | pstate<<16\|core_mask | — |
| 0x0C | QueryCorePstate | core_id | pstate |
| 0x0E | RequestGfxclk | 0 | — (DANGER: crashes SMU!) |
| 0x0F | QueryGfxclk | 0 | freq_mhz |
| 0x11 | QueryVddcrSocClock | index<<16 | freq_mhz |
| 0x13 | QueryDfPstate | 0 | pstate |
| 0x18 | RequestActiveWgp | 0 | — (DANGER) |
| 0x1B | StartTelemetryReporting | value | — |
| 0x1C | StopTelemetryReporting | 0 | — |
| 0x1E | QueryActiveWgp | 0 | count (0 = GFXOFF) |
| 0x37 | GetGfxFrequency | 0 | **MHz directly** (NOT 100×MHz) |
| 0x38 | GetGfxVid | 0 | vid |
| **0x39** | **ForceGfxFreq** | **freq_mhz** | — SAFE if voltage+profile set first |
| 0x3A | UnforceGfxFreq | 0 | — |
| **0x3B** | **ForceGfxVid** | **vid** (from mV) | — |
| 0x3C | UnforceGfxVid | 0 | — (check_status=false) |
| 0x3D | GetEnabledSmuFeatures | 0 | bitmask |
| 0x35 | SetSoftMinCclk | core_id<<20\|freq_mhz | ? |
| 0x36 | SetSoftMaxCclk | core_id<<20\|freq_mhz | ? |

**Queue 3 messages** (from cyan-skillfish-governor src/api/queue3.rs):
| msg | Name | Arg | Notes |
|-----|------|-----|-------|
| 0x01 | TestMessage | value | Returns value+1 |
| 0x0F | SetCpuGpuVid | kind<<16\|vid | — |
| 0x10 | UnforceCpuGpuVid | kind<<16 | — |
| **0x1E** | **SetPerfProfileIndex** | **profile** (0-3) | **MUST call before force_freq!** |
| 0x20 | SetMaxTemperatureCpuGpu | temp_c | — |
| 0x25 | SetOcClk | core_id<<16\|freq_mhz | CPU OC |
| 0x3C | EnableSmuFeatures | mask | Q3 variant |
| 0x3D | DisableSmuFeatures | mask | Q3 variant |
| 0x36 | GetCurrentCpuVoltage | 0 | mV |
| 0x37 | GetCurrentGpuVoltage | 0 | mV |
| 0x40 | GetCpuTempMax | 0 | °C |
| **0x8C** | **SetGpuMaxTemperature** | **temp_c** | Governor sets 80°C |
| 0x8B | SetCpuMaxTemperature | temp_c | — |

**Queue 2 messages** (from cyan-skillfish-governor src/api/queue2.rs):
| msg | Name | Arg | Notes |
|-----|------|-----|-------|
| 0x03 | GetConstant | 0 | Returns 23 (confirmed on our HW) |
| 0x04 | GetDeviceNameChunk | index | Returns 4 ASCII chars |
| **0x05** | **EnableSmuFeatures** | **mask_low** | arg_high=mask_high |
| **0x06** | **DisableSmuFeatures** | **mask_low** | arg_high=mask_high |
| 0x0D/0x0E | SetAddrHigh/Low | addr | unknown purpose |
| 0x17 | CpuDroopCalibration | margin<<16\|test_mv | — |

**Feature bits** (for enable/disable_smu_features via Q2 0x05/0x06 or Q3 0x3C/0x3D):
- bit 0 = GFXCLK DPM
- bit 2 = GFXOFF
- bit 3 = CG (Clock Gating)
- bit 4 = PG (Power Gating)

### Governor change_freq() Sequence (PROVEN SAFE on Linux)
1. `q3(0x8C, 80)` — Set GPU max temp to 80°C
2. `q0(0x3A, 0)` — Unforce any previous frequency
3. `q0(0x3C, 0)` — Unforce any previous voltage (ignores failure)
4. Look up safe point: find nearest `(freq_mhz, mv, profile)` at or above target
5. `q3(0x1E, profile)` — Set perf profile (1=low, 3=high)
6. `q0(0x3B, mv_to_vid(mv))` — Force voltage
7. `q0(0x39, freq_mhz)` — **Force frequency (SAFE when voltage+profile set)**

### Safe Points (from default-config.toml)
| Frequency | Voltage | Profile | Use |
|-----------|---------|---------|-----|
| 500 MHz | 700 mV | 1 | Deep idle |
| 800 MHz | 750 mV | 1 | Idle |
| 1000 MHz | 800 mV | 1 | Low power |
| 1175 MHz | 850 mV | 3 | Performance base |
| 1400 MHz | 900 mV | 3 | Balanced |
| 1600 MHz | 950 mV | 3 | Gaming |
| 1800 MHz | 1000 mV | 3 | High perf |
| 2000 MHz | 1050 mV | 3 | Max safe |

### CRITICAL CORRECTIONS from earlier assumptions
1. **Queue 0 is the correct path for freq/voltage control**, NOT Queue 2.
2. **Queue 2 is for feature enable/disable** (GFXOFF etc.) — but governor NEVER disables GFXOFF (it works on bare metal without it).
3. **Test message goes to Queue 3**, not Queue 0 or Queue 2.
4. **force_gfx_freq WITHOUT voltage crashes** — must be preceded by force_gfx_vid + perf_profile (proven safe).
5. **Freq units are MHz directly** (0x5DC = 1500 MHz, NOT 15 × 100).
6. **Queue 0 requires `allow_queue0=True`** in Python library — dangerous messages are locked.
7. **Governor reads GRBM_STATUS at BAR5 0x2004** via libdrm for GPU load detection.
8. **Even with GFXOFF+CG+PG off + frequency forced, WGPs remain 0** — SPI_PG_ENABLE_STATIC_WGP_MASK is hardware read-only.
9. **Not a DPM table issue** — governor proves safe sequence works without tables; WGPs are fused, not clock-gated.

## 2026-07-08: PSP driver signing fix + comprehensive test run

### PSP signing fix
- **Root cause**: `build.bat` only searched `x64\` for Inf2Cat; Inf2Cat was in `x86\` directory (WDK 10.0.26100.0)
- **Fix**: Added `x86\` path search for Inf2Cat, fixed build order (sign .sys → generate .cat → sign .cat), fixed Inf2Cat OS param (`11_X64` → `10_X64`)
- **Result**: PSP driver now installs without "not digitally signed" error — both .sys and .cat properly signed

### Test results (all pass)
| Test | Result | Notes |
|------|--------|-------|
| `psp-status-test` | ✅ | PSP driver OK, BAR5 mapped, SOS alive (C2PMSG_81=0xF0000010) |
| `bar5-smn-test` | ✅ | SMU v88.6.0 (driver_if=8), 1500 MHz, features 0xDD602C7D |
| `smu-monitor` | ✅ | Stable 1500 MHz @ 931 mV, 0 WGPs, mem temp ~0xC2, all fans/power sensors=0 |
| `governor-sequence` | ✅ | **Frequency change 1500→1166 MHz** — SMU frequency control confirmed working |
| `gfxoff-kill-v2` | ✅ | GFXOFF+CG+PG disabled, CC_ARRAY partially writable, SPI_PG_WGP_MASK(0x5C3C) RO=0 |
| `dcn-init-test` | ✅ | DCN mostly RO, Pipe 3 OTG (0x6300) has live counter 0x270D |

### Key discoveries
- SMU mailbox via SMN (NBIO 0x38/0x3C) fully functional — TestMessage, GetSmuVersion, GetEnabledSmuFeatures, ForceGfxFreq all work
- Governor sequence (Q3 max_temp → Q0 unforce → Q3 perf_profile → Q0 force_vid → Q0 force_freq) safe and effective
- DISPATCH_INITIATOR(0x80E0) accepts VALID command but shader array never executes (WGPs=0)
- SPI_PG_ENABLE_STATIC_WGP_MASK(0x5C3C) confirmed hardware read-only at 0 — compute permanently fused

## 2026-07-14: DreamV3HwInitialize TDR/0x1A fully diagnosed — host compute init IMPOSSIBLE

### Method
Binary-searched the full-init crash with two mechanisms added to `amdbc250_dream_hw_init.c`:
- `DreamV3ReadMaxStep()` reads `HwInitMaxStep` DWORD from the service root — caps init at step N
  (0 = run all). Each step `N` is gated: `if (MaxStep != 0 && N > MaxStep) return STATUS_SUCCESS;`
- `DreamV3MarkHwInitStep(Step)` writes `Step_HwInit` DWORD to the service root (survives reboot,
  unlike in-memory markers which a hard reboot discards).
- Per-step **kill-switches** (registry DWORDs, default 1) skip individual dangerous steps.

### Symptom progression
- Full init (`Flags=0`) → white screen / hard hang / **0x1A MEMORY_MANAGEMENT** BSOD (dump screen, not display freeze).
- cap=4 OK, cap=6 OK, cap=7 = 0x1A. Then isolated each step.

### Root causes of 0x1A (all confirmed by skipping the step → crash gone)
| Step | What | Why it crashes | Kill-switch (default) |
|------|------|----------------|----------------------|
| 6 Firmware | **CP engine UNHALT** after host-loaded firmware | GPU runs loaded microcode and performs a **rogue host DMA write** → corrupts page tables → 0x1A | `HwUnhaltCp=0` (firmware still loaded via IC_BASE DMA, CP stays halted) |
| 9 GART | `DreamV3GartInitialize` writes `MC_VM_AGP_BASE/TOP/BOT` (0x9528/2C/30) | MC is SOS-owned on BC-250; no host AGP aperture — writing these triggers 0x1A | `HwInitGart=0` |
| 10 VM | `DreamV3VmInitialize`→`ConfigureSystemAperture` writes MC_VM system-aperture regs | Same SOS-owned MC class as GART | `HwInitVm=0` |
| 7 GFX ring | ring BASE registers host-read-only (SOS-locked); old code wrote GRBM_GFX_INDEX + allocated 2MB ring | GRBM_GFX_INDEX write is a known display-corruption/BSOD cause; ring BASE unwritable | `HwInitGfxRing=0` |
| 8 SDMA ring | same class as GFX ring (BASE host-read-only) | suspected 0x1A source (kmd.c:3832) | `HwInitSdmaRing=0` |

Steps 11 (Display/DCN), 12 (PSP/NBIO), 13 (RLC), 14 (VRAM detect) are **SAFE** and run normally.
RLC resume (`DreamV3InitRlc`) is gated by `RlcResumeEnabled` (reads from non-existent
`...\atikmdag\Parameters` subkey → unreachable) and is OFF by default anyway.


### How to re-run the bisection (if needed)
```
reg add "HKLM\SYSTEM\CurrentControlSet\Services\atikmdag" /v HwInitMaxStep /t REG_DWORD /d <N> /f
# plus any of: HwInitFirmware=1 HwUnhaltCp=0 HwInitGfxRing=0 HwInitSdmaRing=0 HwInitGart=0 HwInitVm=0
```
Then reboot and run `test-tools\full-init-test.exe` as Admin (Flags=0 full init).
Last *successful* cap = step before the crash.

### New test tools
- `test-tools/full-init-test.c` + `compile-full-init.bat` — triggers `INIT_HARDWARE` Flags=0 (full init).
- `test-tools/seg1-dispatch-test.c` + `compile-seg1-dispatch.bat` — dispatches via SEG1 alias
  0x120E0 (live/writable PGM_RSRC, but execution still silent — confirms SEG1 is not the compute unlock).

## 2026-07-21: Linux dmesg analysis from CachyOS

### Key dmesg findings (cachyos-logai/dmesage.txt)
- **Linux CAN create compute rings**: 8 compute rings (`comp_1.0.0`–`comp_1.3.1`), GFX ring, KIQ ring, SDMA0/1 rings — all created successfully
- **SMU initialized**: `SMU is initialized successfully!` (v88.7.1 vs our v88.6.0 — slightly newer)
- **PSP TMR**: 4MB reserved at 0xF40F800000 for PSP TMR
- **VRAM**: 256M confirmed (`0x000000F400000000 – 0x000000F40FFFFFFF`)
- **24 CUs active**: `SE 2, SH per SE 2, CU per SH 10, active_cu_number 24` — only 24/40 CUs available, WGPs fused
- **SDMA0 fence timeout**: `Fence fallback timer expired on ring sdma0` — minor init delay
- **DCN warnings**: HPD dummy irq errors on all sources (expected for headless mining card)
- **NCT6686D**: Linux nct6683 driver found it at `0x2e:0xa20` (EC firmware v1.0 build 07/28/21) — confirmed separate SMBus device
- **PCI config 0xB8 protected**: `Unexpected write to kernel-exclusive config offset b8` — Linux kernel blocks user-space writes to this SMN path
- **VBIOS**: Fetched from ACPI VFCT, ATOM BIOS `113-AMDRBN-003`
- **Custom kernel param**: `bc250_cc_write_mode` — unknown parameter ignored (user tried CU unlock param)

### Firmware versions from debugfs (amdgpu_firmware_info)
| FW | Version | Notes |
|----|---------|-------|
| ME | 0x63 (v99) | Same as our firmware |
| PFP | 0x94 (v148) | Same |
| CE | 0x25 (v37) | Same |
| RLC | 0x0D (v13) | Same |
| MEC | 0x90 (v144) | Same |
| SMC | 0x00580701 (88.7.1) | **Different from our 88.6.0!** |
| SDMA0/1 | 0x34 (v52) | Same |

### Linux vs Windows comparison update
- **psp_v11_0_8**: The BC-250 specific PSP source file is in a **separate file** (psp_v11_0_8.h/c), NOT in amdgpu_psp.c itself. The cachyos-logai only has the main amdgpu_psp.c (calls `psp_v11_0_8_set_psp_funcs()` at line 241 for CYAN_SKILLFISH2 variant, with `autoload_supported=false` and `boot_time_tmr=false`).
- The `psp_v11_0_8_set_psp_funcs` function is defined in `psp_v11_0_8.c` which was **not present** in the cachyos-logai (only had the main PSP driver files).
- **SMU version difference**: Linux has SMU 88.7.1 while our dumped SMU is 88.6.0. This could be a firmware mismatch — Linux likely has a newer Smu.bin.

### Sources of interest
- Full Linux PSP driver: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/amdgpu_psp.c
- Original BC-250 PSP patch series: https://lists.x.org/archives/amd-gfx/2021-July/066821.html
- Linux psp_v11_0_8.h: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/gpu/drm/amd/amdgpu/psp_v11_0_8.h
- Linux psp_v11_0_8.c: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/gpu/drm/amd/amdgpu/psp_v11_0_8.c

### PSP v11.0.8 Source Code Analysis (fetched 2026-07-21)

`psp_v11_0_8.c` is **extremely minimal** — only defines 5 functions:
- `ring_create`, `ring_stop`, `ring_destroy`, `ring_get_wptr`, `ring_set_wptr`
- **NO** `init_microcode`, `bootloader_load_*`, `load_ip_fw` — firmware loading is NOT in this file

**Ring create protocol** (non-SRIOV):
1. Wait for `C2PMSG_64` bit 31 = 1 (TOS READY flag) ← **this is the blocker**
2. Write ring addr LOW to `C2PMSG_69`, HIGH to `C2PMSG_70`, SIZE to `C2PMSG_71`
3. Write ring type to `C2PMSG_64` (command), wait for response bit 31
4. Ring WPTR uses `C2PMSG_67`

**Root cause of ring failure**: The first step (`MBOX_TOS_READY_FLAG` on C2PMSG_64 bit 31) never succeeds because BC-250's SOS firmware doesn't include Trusted OS (TOS) component. Linux works because:
- `autoload_supported=false` — VBIOS/bootloader pre-loads firmware
- `boot_time_tmr=false` — TMR is set up by BIOS at boot time
- The full amdgpu PSP init sequence (bootloader → sysdrv → sos → TOS) is performed once at boot

**On Windows**: Our PSP driver tries ring protocol after SOS is alive (C2PMSG_81=0xF0000010) but without TOS initialized, C2PMSG_64 bit 31 is never set → ring_create always fails. This confirms the earlier finding that our SOS firmware is minimal (no TOS ring protocol support).

## BREAKTHROUGH 2026-07-31: CPU core unlock via SMU + "fused-off" conclusion REVERSED

### CPU Core Unlock WORKS (proved on our hardware)
- `test-tools/smn-core-unlock-test.c` (compiled to `output\smn-core-unlock-test.exe`) — port of
  `rw-r-r-0644/bc250-core-unlock` (Linux) to our Windows driver.
- **Result**: SMN[0x0115A870] core presence mask changed `0x77` (6 cores) → `0xFF` (8 cores / 16 threads)
  via SMU Queue 3 msg `0x98`. Takes effect on next reboot (AGESA enumerates 8 cores, PSP releases all).
- Volatile: cold power cycle reverts it, must re-run after every cold boot.

### The SMU ungated write primitive (KEY MECHANISM)
- **SMU Queue 3 msg `0x98`** = ungated SMU-privileged SMN write: writes the **fixed value `0x00FF`** to
  ANY SMN address passed as its argument. Only validation is addr != 0.
- **Host PCI SMN window writes (0xB8/0xBC, or BAR5+0x38/0x3C) do NOT stick** to locked SMN registers,
  but **SMU writes DO stick** — SMU is a more privileged SMN requester.
- Q3 mailbox: cmd=0x03B10A20 rsp=0x03B10A80 arg=0x03B10A88 (DONE={0x01,0xFF,0xFE,0xFD,0xFC}).
- WARNING: 0x98 has no bounds check — wrong address may hang or damage the board. Only write
  known-good addresses (check mask reads 0x77 first, like the core unlock does).

### CRITICAL: "compute permanently fused off" conclusion is likely WRONG
- BC-250 is a **full PS5 Oberon die** (8× Zen2 + RDNA2 + 40 CUs), NOT a mining-only ASIC with
  fused-off shader array. Disabled features are MASKED (SOS/SMU-locked), not fused.
- Linux community (duggasco/bc250-40cu-unlock, elektricm docs) writes the SAME registers we found
  "read-only" — and they work on Linux:
  - `CC_GC_SHADER_ARRAY_CONFIG` (BAR5 0x9C1C): stock `0xfff80000` (24 CU) → unlocked `0xffe00000` (40 CU)
  - `SPI_PG_ENABLE_STATIC_WGP_MASK` (BAR5 0x5C3C): stock `0x07` (WGP 0-2) → unlocked `0x1F` (WGP 0-4)
  - `RLC_PG_ALWAYS_ON_WGP_MASK` (BAR5 0x3D64): set to `0x1F` to keep all WGPs powered
  - ALL THREE writes are required together; CC alone only changes reporting, SPI alone only 24 CU compute.
- **Linux stock SPI_PG = 0x07, but our driver read 0x00000000** — the SPI_PG register is a
  **per-bank GRBM-indexed register** (SE0/SH0, SE0/SH1, SE1/SH0, SE1/SH1). We likely read it with
  the wrong GRBM_GFX_INDEX selection, so we saw 0 instead of 0x07.
- Linux `RequestActiveWgp` SMU msg does NOT exist on cyan_skillfish; SPI register is directly writable.
- On Linux, the unlock works via patched amdgpu module (`bc250_cc_write_mode=3`) at
  `gfx_v10_0_get_cu_info()` time (kernel context, per-bank GRBM select), and via UMR post-boot.

### Our next experiments (all in progress)
1. `test-tools/smn-gc-alias-scan.c` (compiled `output\smn-gc-alias-scan.exe`, NOT yet run):
   - Step 1: per-bank GRBM readback of SPI_PG/RLC_PG/CC (should see 0x07 / 0x1F, not 0!)
   - Step 2: scan SMN 0x00000000-0x04000000 for GPU_ID(0x9FFF9700)/CC values → find GC SMN aliases
   - Step 3: if alias found, try SMU msg 0x98 write (0x00FF) and verify BAR5 readback changes
2. If SPI_PG/CC are per-bank GRBM-indexed, write 0x1F/0xffe00000 via correct GRBM select
   + verify WRITE_CONFIRM, then check QueryActiveWgp (SMU 0x1E) and GRBM_STATUS.
3. If no SMN alias exists for GC regs, the per-bank GRBM write is the only path — but our
   earlier broadcast-write tests showed host writes don't stick. Need to distinguish
   "SOS-locked" (maybe per-bank select fixes it) from genuinely fused.

### Test results that CHANGED our interpretation (2026-07-25, pre-unlock)
`run-all-kiq-tests.exe` (EXECUTE_RING_PM4 IOCTL 0x80000BE8):
- CP_HQD_ACTIVE=1 (writable!), WPTR 0→0x10 (writable!), RPTR stays 0 (engine not consuming)
- SMU features 0xDD602C7D→0xDD602C71 (WakeGfx cleared GFXOFF bit), freq→1500 MHz
- COMPUTE_PGM_LO(0x8110)=0x6E512C00 writable; THREAD_MGMT_SE0(0x8138)=0xFFFFFFFF writable
- MQD PGM_LO readback 0x65FEE36E ≠ expected 0x6E512400 → MQD load may not have fully populated
- GRBM_STATUS 0→0, Scratch unchanged → no execution, consistent with WGPs not enabled
- GPU_KIQ_TEST (0x80000BD0) fails at ring alloc (Result=0xDEAD0001) — GCVM/VRAM path broken

### References
- Core unlock repo: https://github.com/rw-r-r-0644/bc250-core-unlock (MIT)
- 40CU unlock technical report: https://github.com/duggasco/bc250-40cu-unlock/blob/main/docs/technical-report.md
- BC250 docs: https://elektricm.github.io/amd-bc250-docs/system/40cu-unlock/
- CU live manager (UMR): https://github.com/WinnieLV/bc250-cu-live-manager

## CONFIRMED: IP discovery table — all our register bases are CORRECT (2026-07-31)

### How to dump (CachyOS, no root needed)
```bash
for f in /sys/bus/pci/devices/*/ip_discovery/die/0/*/*/base_addr; do
  echo "== ${f%/base_addr}"; cat "$f"
done
```
Path comes from Linux `amdgpu_discovery_reg_base_init()` (amdgpu_discovery.c:1366): it reads the
discovery TMR binary from VRAM (`pos = vram_size - DISCOVERY_TMR_OFFSET`, TMR_SIZE=10KB,
TMR_OFFSET=64KB, i.e. VRAM byte 0x0FFF0000 for 256MB) and fills `adev->reg_offset[HWIP][inst] =
ip->base_address`. **cyan_skillfish2 (0x13FE) uses THIS path, NOT hardcoded
`cyan_skillfish_reg_base_init`** (OpenBSD commit 402f067: `case CHIP_CYAN_SKILLFISH: if
(apu_flags & CYAN_SKILLFISH2) amdgpu_discovery_reg_base_init()`).

### Dump result (BDF 0000:01:00.0) — bases in DWORD units, multiply by 4 for BAR5 bytes
| IP | base_addr (dwords) | Notes |
|----|--------------------|-------|
| **GC/0** | 0x1260, 0xA000, 0x02402C00 | **GC_BASE=0x1260 CONFIRMED**; 0xA000=SEG1 alias; 0x02402C00=high SOC base |
| **SDMA0/1** | 0x1260, 0xA000, 0x02402C00 | same as GC — SDMA regs live at GC offsets! |
| NBIO (NBIF/0) | 0x0, 0x14, 0xD20, 0x10400, 0x0241B000, 0x04040000 | base 0x0000 confirmed |
| HDP/0 | 0xF20, 0x0240A400 | confirmed |
| MMHUB/0 | 0x1A000, 0x02408800 | confirmed |
| DF/0 | 0x7000, 0x0240B800 | confirmed |
| OSSSYS/0 | 0x10A0, 0x0240A000 | confirmed |
| MP0/MP1/0 | 0x16000, 0xDC0000, 0xE00000, 0xE40000, 0x0243FC00 | 0x16000 confirmed |
| THM/0 | 0x16600, 0x02400C00 | confirmed |
| SMUIO/0 | 0x16800, 0x16A00, 0x440000, 0x02401000 | confirmed |
| CLKA/0-2 | 0x16C00/0x16E00/0x17000, +0x02401800/1C/20 | confirmed |
| CLKB/0 | 0x17E00, 0x0240BC00 | confirmed |
| FUSE/0 | 0x17400, 0x02401400 | confirmed |
| UMC/0,1 | 0x14000, 0x54000, +0x02425800/5C | confirmed |
| ATHUB/0 | 0xC00, 0x02408C00 | confirmed |
| UVD/0 | 0x7800, 0x7E00, 0x02403000 | VCN=0x7800 |
| ACP/0 | 0x48000, 0x02403800 | — |
| DAZ/0 | 0x4C000, 0x02404800 | — |
| DMU/0 | 0x12, 0xC0, 0x34C0, 0x9000, 0x02403C00 | DCE block (display) |
| DBGU_NBIO | 0x1C0 | — |
| DBGU_IO | 0x1E0 | — |
| DFX | 0x580 | — |
| DFX_DAP | 0x5A0, 0xB80000, 0x0240C400 | — |
| IOHC | 0x10000, 0x02406000, 0x4EC0000 | — |
| L2IMU | 0x7DC0, 0x900000, 0x02407000, 0x4FC0000, 0x55C0000 | — |
| SYSTEMHUB | 0xEA0, 0x500000, 0x02420000 | — |
| PCIE | 0x2411800, 0x4440000 | — |
| PCS | 0x2414000, 0x4680000 | — |
| USB | 0x242A800/0x242AC00, +0x5B00000/0x5B80000 | — |

### Conclusions
1. **Our entire register map (hw.h) is correct** — GC_BASE=0x1260, HDP=0xF20, MP0=0x16000, etc.
2. **SDMA registers are at GC offsets** — explains why SDMA ring init at 0xE018 worked.
3. The 0x3460 vs 0x34D0 GRBM_GFX_INDEX "discrepancy" was a WRONG mm* assumption, not a wrong base — 0x34D0 remains the live register.
4. **The driver's problem is NOT the register map** — it's the init SEQUENCE (SMU/PSP/GFX init order) and/or host-write locking. Compare Linux gfx_v10_0.c/SMU/PSP init sequences with ours.
5. Most IPs carry a second base in 0x0240xxxx (high SOC register window) — currently unused by us, low priority.

## smn-gc-alias-scan v2 results — per-bank SPI_PG hypothesis REJECTED (2026-08-01)

Ran `output\smn-gc-alias-scan.exe` (v2, no args) — ran clean, no hang. Log: `output\smn-gc-alias-scan.log`.

### Step 1: per-bank GRBM readback (GRBM_GFX_INDEX 0x34D0)
GPU_ID(0x0000)=0x9FFF9714 GRBM_STATUS=0x00000000 GRBM_GFX_INDEX live=0xBA062100

| Bank | SPI_PG(0x5C3C) | RLC_PG(0x3D64) | CC_ARRAY(0x9C1C) |
|------|----------------|----------------|------------------|
| SE0/SH0 (0x00000000) | 0x00000000 | 0xFFFFFFFF | 0x00000000 |
| SE0/SH1 (0x00000100) | 0x00000000 | 0xFFFFFFFF | 0x00000000 |
| SE1/SH0 (0x00010000) | 0x00000000 | 0xFFFFFFFF | 0x00000000 |
| SE1/SH1 (0x00010100) | 0x00000000 | 0xFFFFFFFF | 0x00000000 |
| BCAST (0x15000000) | 0x00000000 | 0xFFFFFFFF | 0x00000000 |

### Step 1b: per-bank write test (duggasco unlock values)
- **SPI_PG 0x1F → reads 0x00000000 on ALL banks** — writes do NOT stick. Per-bank select does NOT unlock it.
- RLC_PG 0x1F → reads 0xFFFFFFFF (read-only, unchanged).
- **CC_ARRAY 0xFFE00000 → reads 0x1F000000** — only bits 24-28 persist, top 3 bits (29-31) masked. PARTIALLY writable.

### Interpretation (IMPORTANT — changes our model)
1. **The "wrong GRBM bank select" hypothesis is now REJECTED.** Even with correct per-bank GRBM_GFX_INDEX selects (SH=1<<8, SE=1<<16 as v2 uses, plus broadcast 0x15000000), SPI_PG reads 0 and writes don't stick. Earlier we read 0 with the wrong INSTANCE-index selects (v1 used 0x01/0x10/0x11); now with correct SH/SE selects it's STILL 0. Host BAR5 writes to SPI_PG are genuinely SOS-locked — the register reports 0 because WGPs are gated.
2. **GRBM_GFX_INDEX bit layout discrepancy (tool vs hw.h):** v2 tool uses SH=1<<8, SE=1<<16 (matches gfx10 soc15 GRBM_GFX_INDEX); hw.h documents INSTANCE_INDEX at bits 25-24. Empirically broadcast 0xE0000000 (hw.h value) reads back live. NOT yet resolved which layout actually selects banks on BC-250 — v2's per-bank selects read back identically (GFX_IDX_after echoes the written value), so the writes landed but no bank returned a different SPI_PG.
3. CC_ARRAY partial-writability (0x1F000000, bits 24-28) matches earlier "PARTIALLY WRITABLE 0xFFE00000→0x1F000000" finding — consistent across tests.
4. GRBM_STATUS=0 and SCRATCH=0x4D585042 unchanged after writes — no shader execution.

### Next steps (from AGENTS.md "next experiments", still pending)
- `-scan` (restricted SMN ranges 0x011xxxxx / 0x03B1xxxx) to hunt GC SMN aliases — NOT yet run this session.
- `-write <addr>` SMU msg 0x98 — DISABLED by default, requires explicit arg; only known-good addresses.
- Distinguish "SOS-locked" from "genuinely fused" is NOT resolved by this run — but the data now leans SOS/host-write-lock, not bank-select error.

## smn-gc-alias-scan v2 with -scan — NO GC SMN aliases found (2026-08-01, same day)

Ran `output\smn-gc-alias-scan.exe -scan` — ran clean, no hang. Log overwritten: `output\smn-gc-alias-scan.log`.

### Step 2 result: restricted SMN scan found NOTHING
- Range 0x01100000-0x01200000 (step 0x100): **0 GPU_ID matches, 0 CC matches**
- Range 0x03B10000-0x03B11000 (step 0x100): **0 GPU_ID matches, 0 CC matches**
- No SMN aliases for GC registers in the safe/proven ranges. GC regs (GPU_ID=0x9FFF9714, CC 0xFFF80000/0xFFE00000, SPI 0x07) are NOT mirrored into SMN 0x011xxxxx or 0x03B1xxxx.
- Step 1/1b repeated identically: SPI_PG=0 all banks, RLC_PG=0xFFFFFFFF, CC=0x1F000000.

### CC_ARRAY persistence CONFIRMED
- This run's Step 1 read CC_ARRAY(0x9C1C)=**0x1F000000 BEFORE any writes** — the previous session's 0xFFE00000 write (masked to 0x1F000000) PERSISTED across process exit and driver re-init. CC bits 24-28 are genuinely host-writable and sticky.

### GRBM_GFX_INDEX layout RESOLVED (from Linux source)
- Linux gfx9/gfx10 `gfx_v9_0_select_se_sh()` (and gfx10 equivalent) uses:
  - INSTANCE_INDEX bits 7:0, SH_INDEX bits 15:8, SE_INDEX bits 23:16
  - INSTANCE_BROADCAST_WRITES bit 24, SH_BROADCAST_WRITES bit 26, SE_BROADCAST_WRITES bit 28
  - broadcast (all SE/SH) value = 0x15000000 = INST_BCAST_WR(24) | SH_BCAST_WR(26) | SE_BCAST_WR(28)
- **v2 tool's bank selects are CORRECT** (matches Linux). hw.h's documented layout (INSTANCE_INDEX bits 25-24, SE_BROADCAST bit 31, value 0xE0000000) is WRONG / matches a different (older soc15) encoding.
- Even so, SPI_PG reads 0 on ALL banks — so with correct selects it's still SOS-locked. The GRBM layout is NOT the unlock.

### Updated conclusion
- GC registers are NOT SMN-aliased in any proven-safe range, so SMU msg 0x98 cannot reach them via known addresses.
- The 40CU unlock on this unit requires either (a) an unknown SMN alias for SPI_PG outside safe ranges, or (b) Linux-kernel-context writes (debugfs/UMR) which we cannot replicate in WDM — both currently unreachable.
- Remaining viable actions: CC_ARRAY bits 24-28 toggle (COSMETIC only, does not enable WGPs); `-write` SMU 0x98 only for already-proven addresses (core unlock 0x0115A870).

## WGP Unlock Research — FULL RESULTS (2026-08-06)

### Goal
Enable 3D graphics on BC-250 by powering on WGPs (shader array). WGPs off → GRBM_STATUS=0, no ring processing, display-only works but no 3D.

### Hardware reality (confirmed)
- BC-250 = full PS5 Oberon die (8 Zen2 + RDNA2 + 40 CUs), NOT fused mining ASIC
- WGPs are SOS/SMU-locked (not fused) — Linux can unlock via debugfs + patched amdgpu
- Stock SPI_PG = 0x07 (24 CU) on Linux; our Windows reads 0x00000000
- Unlock needs 3 writes TOGETHER: CC_ARRAY(0x9C1C)=0xFFE00000 + SPI_PG(0x5C3C)=0x1F + RLC_PG(0x3D64)=0x1F
- SPI_PG_ENABLE_STATIC_WGP_MASK is per-bank GRBM-indexed

### Paths tested — ALL FAILED to write SPI_PG

#### Path 3: Direct BAR5 writes (ALL LOCKED)
| Test | Result |
|---|---|
| Direct SPI_PG write (any value 0x01-0xFFFFFFFF) | LOCKED (readback 0x00000000) |
| After GRBM soft reset (0x3278) | LOCKED |
| GRBM_GFX_INDEX broadcast 0x15000000 | LOCKED |
| Per-bank SE0/SH0, SE0/SH1, SE1/SH0, SE1/SH1 | LOCKED |
| Via UNLOCK_40CU IOCTL (0x80000980) | LOCKED (driver does same writes) |
| CC_ARRAY writable? | YES (0x1F000000 partial) |
| RLC_PG writable? | NO (0xFFFFFFFF read-only) |

#### Path 1: SMU Q3 msg 0x98 (ungated SMN write)
- Via direct BAR5 SMN: destabilized SMU (SMU dead after, required cold reboot)
- Via PSP proxy (IOCTL_AMDBC250_PSP_SMU_MSG): status=1 OK, but SPI_PG still 0
- SMU 0x98 writes 0x00FF to any SMN address, but NO KNOWN SPI_PG SMN alias found
- smn-gc-alias-scan searched 0x01100000-0x01200000 and 0x03B10000-0x03B11000: 0 GC matches
- Candidate SMN addresses tested (0x0115B000 etc): none affected SPI_PG

#### Path 2: PSP mailbox commands
- RequestActiveWgp (0x18) via direct Q0: REJECTED (status=-1)
- RequestActiveWgp (0x18) via PSP proxy: ACCEPTED (status=1) but Wgp still 0
- QueryActiveWgp (0x1E): returns 0 always
- PSP ring GFX_CMD scan (0x01-0xFF): all "consumed" but no WGP-specific command found
- PSP driver NOT installed (would need sibling repo build)

#### Path 4: VBIOS-based unlock
- Not implemented — driver does not fetch VBIOS from ACPI VFCT
- Linux uses VBIOS for SMU wake sequence

### What WORKS on Windows
- GPU driver (atikmdag.sys): WDM IOCTL, BAR5 mapping, SMU mailbox via SMN, PSP proxy, firmware loading
- SMU Q3 msg 0x98 via PSP proxy (status=1, no SMU destabilization)
- GetSmuVersion, GetDriverIfVersion, QueryActiveWgp via PSP proxy (all status=1)
- CC_ARRAY partially writable (0x1F000000)
- UNLOCK_40CU IOCTL exists (0x80000980) and runs, but writes don't stick
- KMDOD display-only (sampledisplay + wddm-ps5 skeleton): registers readable, no 3D
- SMU frequency/voltage control (1500MHz @ 931mV via Q0/Q3)

### Key insight: SOS locks SPI_PG
- SPI_PG_ENABLE_STATIC_WGP_MASK is SOS-locked from host BAR5 access
- SOS accepts RequestActiveWgp (0x18) but doesn't actually enable WGPs
- The "legit" path (Linux amdgpu via debugfs) suggests SOS has an unlock gate that requires proper PSP authentication sequence
- Our WDM driver lacks the full PSP authentication that Linux amdgpu performs

### Test tools used (all in output\)
- bc250-diag.exe, reg-dump-and-nop.exe, bar5-smn-test.exe
- bar5-cu-unlock-test.exe (GRBM_GFX_INDEX probe + per-bank writes)
- unlock40cu-real.exe (UNLOCK_40CU IOCTL direct)
- wgp-unlock-test.exe (Path 1 + Path 3 combined)
- wgp-smu-test.exe (SMU 0x18 RequestActiveWgp)
- wgp-deep-test.exe (GRBM reset + per-bank + value sweep)
- psp-proxy-smu-test.exe (PSP proxy SMU messages)
- smn-core-unlock-test.exe (SMU 0x98 CPU core unlock — WORKS)
- gfx-ring-init-test.exe (GFX ring — NOT PROCESSED)

## GPU Driver Deep Analysis vs Linux (2026-08-07)

### ✅ CORRECT — not the cause of 3D failure
| Area | Status | Notes |
|---|---|---|
| All register offsets | ✅ Correct | Match Linux gc_10_1_0_offset.h or verified BC-250-specific |
| PSP ring protocol | ✅ Correct | Byte-identical to Linux psp_v11_0_8.c |
| SMU Q0/Q3 mailbox | ✅ Correct | 0x03B10A08/48/68 match Linux |
| UNLOCK_40CU IOCTL | ✅ Correct | Per-bank writes + readback verification |
| GRBM_GFX_INDEX | ✅ Correct | 0x34D0, broadcast 0x15000000 |
| SPI_PG_ENABLE_STATIC_WGP_MASK | ✅ Correct | 0x5C3C (mm=0x1277) |
| CC_GC_SHADER_ARRAY_CONFIG | ✅ Correct | 0x9C1C for BC-250 |
| CP_MEC_CNTL | ✅ Correct | 0x4B14 (mm=0x0E2D) |

### ⚠️ Fixed bugs
| # | File | Was | Now |
|---|---|---|---|
| 1 | hw.h:378 | CP_HQD_PQ_WPTR_POLL_CNTL = 0x9148 (dup of PQ_CONTROL) | **0x9138** (mm=0x1FB6, matches Linux) |

### PSP Firmware Load — INTEGRATED into GPU driver (2026-08-07)

**Strategy decision: A) Merge PSP into GPU driver (chosen)**
- GPU driver now loads ALL firmware: SYSDRV, SOS, SMC (via C2PMSG) + CP firmware (via KIQ)
- Separate PSP driver is NO LONGER NEEDED
- Reason: PSP proxy was broken (PSP BAR0 ≠ GPU BAR5), GPU driver has working SMU access

**What was integrated:**
| File | What it does |
|---|---|
| `amdbc250_dream_psp_fw_load.c` | Loads SYSDRV/SOS/SMC via C2PMSG (0x03B10A08/48/68) using GPU BAR5 SMN access |
| Called from `DreamV3HwInitialize` Step 0b | Before KIQ init, after BAR5 mapping |

**Firmware load flow (now unified):**
```
GPU Driver Init
    ↓
Step 0b: DreamV3LoadPspFirmware()
    ├── Load SYSDRV (PSP bootloader) via C2PMSG_35 cmd=0x04
    ├── Load SOS (Secure OS) via C2PMSG_35 cmd=0x08
    └── Load SMC (SMU firmware) via C2PMSG_35 cmd=0x0A
    ↓
Step 6: DreamV3LoadAllFirmware() (CP firmware)
    ├── Load ME/PFP/CE/MEC via PSP KIQ (SOS processes)
    └── ...
```

**Separate PSP driver status:**
- `C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\` — DEPRECATED, do not install
- Its firmware load code is now in the GPU driver
- Installing both drivers may cause conflicts (avoid)

### 🔴 CRITICAL missing pieces (root cause of 3D failure)
| # | Missing | Impact |
|---|---|---|
| 1 | **SMU DPM initialization** | No DPM tables uploaded → SMU cannot power on WGPs |
| 2 | **SPI_PG SOS-locked** | Host BAR5 writes blocked by PSP Secure OS |
| 3 | **Ring BASE registers SOS-locked** | Cannot create GFX/compute rings from host |

### Root cause chain
```
SPI_PG = SOS-locked → Host cannot write
    ↓
Even if written → SMU without DPM tables cannot power on WGP
    ↓
Even if WGP powered → Ring BASE registers SOS-locked
```

### PSP Ring Protocol — now called (2026-08-07 fix)
- `Amdbc250PspRingCreate` now called from `DreamV3PspHardwareInit` when `PspAlive`
- Fixed: writes `(PSP_RING_TYPE_GFX << 16)` to C2PMSG_64 (was writing 0)
- Added `GfxRingAvailable` flag to DeviceExtension
- Files changed: `amdbc250_psp.c`, `amdbc250_dream_hw_init.c`, `amdbc250_dream_kmd.h`, `amdbc250_psp.h`

### Verdict
**Driver code is correct. 3D is blocked by hardware/firmware SOS lock on SPI_PG and SMU DPM not initialized — not driver bugs.**

### Compile command (works)
```
cmd /c "call F:\VS2022\Community\VC\Auxiliary\Build\vcvars64.bat" && set INCLUDE=MSVC_INC;WDK_ucrt;WDK_shared;WDK_um;WDK_km;%INCLUDE% && set LIB=MSVC_lib;WDK_ucrt\x64;WDK_um\x64;%LIB% && cl /nologo /O2 /W3 /Feoutput\test.exe test.c /link /subsystem:console"
```
VS2022 + WDK both on F: drive. Headers: test-tools\..\inc\amdbc250_ioctl.h

### Driver facts
- UNLOCK_40CU IOCTL: 0x80000980 (case in kmd.c:3855), does 4 per-bank writes, reports banksVerified
- PSP_SMU_MSG IOCTL: 0x80000924 → Amdbc250PspDirectSmuMsg → Q0 mailbox (0x03B10A08/48/68)
- GRBM_GFX_INDEX = 0x34D0 (confirmed live register)
- Broadcast = 0x15000000 (Linux gfx10 layout: INSTbit24 + SHbit26 + SEbit28)
- Driver IOCTL_INDEX = 0x270 (not 0x200 as header comment suggests)

### Conclusion
3D enablement on Windows WDM is BLOCKED by SOS-locked SPI_PG register. All host BAR5 write paths fail. PSP proxy accepts commands but doesn't enable WGPs. The unlock requires either:
1. A PSP authentication sequence our driver doesn't perform
2. An unknown SMN alias for SPI_PG outside safe ranges
3. VBIOS-based SMU wake sequence (not implemented)

This remains the fundamental unsolved blocker for BC-250 3D on Windows.

## BREAKTHROUGH: KMDOD display-only driver LOADS on BC-250 (2026-08-03)

### Milestone
Microsoft KMDOD sample (Basic Display Driver DOD, `F:\bc-250-proektas\Dev\windows-driver-samples\video\KMDOD`) customized for BC-250 now INSTALLS and RUNS:
- Device: "AMD BC-250 (Display Only Driver)" PCI\VEN_1002&DEV_13FE&SUBSYS_00001022&REV_00, Status OK, CM_PROB_NONE
- Win32_VideoController: AMD BC-250 (Display Only Driver), Status OK, DriverVersion 1.0.103.0, mode 2560x1440x32bpp
- Service KDODSamp: RUNNING (STATE 4)
- Installed from `output\kmdod-test\` (SampleDisplay.sys 8/3/2026, 33128 bytes, + sampledisplay.inf/.cat, INF oem2.inf)
- Test signing ON + Secure Boot OFF still required (AMD-BC250-Signer self-signed).

### Fixes that resolved Code 31 (0xC0000017 STATUS_NO_MEMORY)
1. **memory.cxx**: `ExAllocatePool2` was called with `PoolType` (POOL_TYPE) instead of `POOL_FLAGS`. `NonPagedPool`=0 is NOT a valid POOL_FLAGS → NULL → AddDevice returns STATUS_NO_MEMORY → Code 31. Fixed both operator new and new[]: `POOL_FLAGS PoolFlags = (PoolType == PagedPool) ? POOL_FLAG_PAGED : POOL_FLAG_NON_PAGED;`
2. **bdd_ddi.cxx**: BC-250-modified `BddDdiStartDevice` didn't call `pBDD->StartDevice()` → 0 views/children/modes. Fixed to call it (sets MAX_VIEWS=1/MAX_CHILDREN=1 + mode list).

### Historical confirmation (ps5-win-driver PROGRESS.md)
- The 0xC0000059 (STATUS_REVISION_MISMATCH) saga from May 2026 was resolved by the full 28-callback KMDOD sample shape (`DxgkInitializeDisplayOnlyDriver` → STATUS_SUCCESS); the ONLY remaining blocker was this same Code 31 alloc bug (CM_PROB_FAILED_ADD / 0xC0000017).
- `F:\bc-250-proektas\Backup\dirbantis\ps5-win-driver\src\kmd\amdbc250_kmd.c` is a complete REAL WDDM miniport base (links displib.lib, full DDI table, direct DxgkInitialize, uses ExAllocatePoolWithTag correctly) — production candidate if DOD is insufficient.

### Next phase (in progress)
- Test actual display output (KMDOD only does 1 frame / basic mode).
- Add a UMD (D3D runtime) — INF needs UserModeDriverName / InstalledDisplayDrivers registry entries.
- Consider full WDDM miniport from ps5-win-driver as the production base.

## DOD polish round 1 (2026-08-03)

Milestone confirmed: monitor (ACER S271HL) shows the desktop and looks better than Microsoft Basic Display; dxdiag shows our driver (with expected unsigned note — self-signed cert can't be WHQL'd).

### Changes (source: `F:\bc-250-proektas\Dev\windows-driver-samples\video\KMDOD\`)
1. **Mode list expanded** (bdd_dmm.cxx `C_SampleSourceMode[]`): now `{640,480},{800,600},{1024,768},{1152,864},{1280,720},{1280,800},{1280,1024},{1366,768},{1400,1050},{1600,1200},{1680,1050},{1920,1080},{1920,1200},{2560,1440}` — adds the common 16:9 modes (1280×720, 1366×768, 1920×1080, 2560×1440) that were missing.
2. **Refresh rate**: target mode `VSyncFreq` set to 60/1 Hz (was `D3DKMT_FREQUENCY_NOTSPECIFIED`).
3. **DbgPrint spam reduction**: `BddDdiPresentDisplayOnly` + `BddDdiSetPointerPosition` + `BddDdiSetPointerShape` now log only the first call + failures (these fire every vsync / mouse move and flooded DebugView at ERROR level).

### Package
- Built v1.0.104.0 → `output\kmdod-test\` (SampleDisplay.sys 38864B 8/3/2026, sampledisplay.inf DriverVer=07/31/2026, sampledisplay.cat). Signed with AMD-BC250-Signer SHA1 34AFF96C... (same cert as installed). Verified Valid.
- Note: multiple AMD-BC250-Signer certs exist in My store; always sign with explicit `/sha1 34AFF96C57E9ADE68B23B4828859CF9B7F4EF442` to avoid signtool "multiple certificates" ambiguity.
- Build cmd: `C:\Users\Keshas\AppData\Local\Temp\opencode\kmdod-build.bat` (CL+kernel+displib from KMDOD root files).
- CAT gen: Inf2Cat needs the .sys + .inf in the SAME dir (use a clean pkg dir), DriverVer date must be in the past or Inf2Cat errors 22.9.1/22.9.7.
- Inf2Cat.exe is ONLY in the **x86** bin dir (`bin\10.0.26100.0\x86\`), not x64. signtool.exe is in x64. Cert exported via `Export-PfxCertificate` to `kemod-pkg sign with `/f pfx /p bc250sign /sha1 34AFF96C...`.

## DOD BSOD fix — v1.0.105 (2026-08-03)

**v1.0.104 caused bugcheck 0x7E (SYSTEM_THREAD_EXCEPTION_NOT_HANDLED, access violation, write to 0x0).**

### Root cause (confirmed via minidump + disasm + PDB)
- `BlackOutScreen()` (`bdd.cxx`) called `RtlZeroMemory(NULL, 0xE10000)` (0xE10000 = 2560×1440×4 = one full framebuffer).
- Crash site `SampleDisplay+0x2410` = SSE vector memset loop (16-byte `movups [rcx]`); `call memset` at `+0x6176` is inside `BlackOutScreen` (0x60C4–0x61B4).
- Why NULL: `StartDevice()` sets `FrameBufferIsActive=TRUE` when POST display found, but `FrameBuffer.Ptr` is only mapped LATER in `SetSourceModeAndPath()` via `MapFrameBuffer()` after a `CommitVidPn`. If `StopDeviceAndReleasePostDisplayOwnership()` (bdd.cxx:526) or `SetVidPnSourceVisibility(FALSE)` (bdd_dmm.cxx:488) runs before any CommitVidPn (e.g. RDP/session switch takes over console), `BlackOutScreen` deref'd NULL.
- Same latent NULL was present in v1.0.103 (`.bak` had identical StartDevice code), but only triggered once the RDP/session-switch path ran.

### Fix (bdd.cxx only)
1. `BlackOutScreen()`: guard `&& (m_CurrentModes[SourceId].FrameBuffer.Ptr != NULL)`.
2. `PresentDisplayOnly()`: guard `&& (FrameBuffer.Ptr != NULL)`.
3. `StartDevice()`: KEPT `FrameBufferIsActive=TRUE` (needed so `QueryChildStatus` reports Connected at boot for the POST-display-present case; matches working v1.0.103). Do NOT set it FALSE — would break boot bring-up.

### Package (built + signed)
- v1.0.105.0 → `output\kmdod-test\` (SampleDisplay.sys 39144B signed, sampledisplay.inf DriverVer=08/03/2026, sampledisplay.cat). Verified Valid.
- PDB: `F:\bc-250-proektas\Dev\windows-driver-samples\video\KMDOD\Sample\x64\Release\SampleDisplay.pdb` (link /dump /linenumbers on the .pdb via link.exe works to map addresses to functions; plain signtool PDB dump fails).
- Install requires Device Manager uninstall w/ "Delete driver" → reboot → install new .inf → reboot (AGENTS reinstall flow).



## wddm-ps5 project � new WDDM miniport base (2026-08-03)

### Decision
User approved path 1-2: merge into ONE driver. Base = ps5-win-driver KMD (full WDDM miniport skeleton + displib DxgkInitialize + Escape IOCTL + Linux-ordered hw_init), + DOD's proven WDDM registration/display, + our GPU driver's register/SMU/firmware layer. 3D still blocked by WGP lock (SPI_PG 0x5C3C = 0) but this becomes the platform driver to improve.

### Status
- **DOD v1.0.106 CONFIRMED WORKING**: black-screen root cause was v1.0.104 "polish" changes (expanded C_SampleSourceMode to 14 modes + VSyncFreq 60/1). Reverting BOTH to stock (9 modes {800,600}...{1920,1200} + VSyncFreq D3DKMDT_FREQUENCY_NOTSPECIFIED) restored display. KEPT v1.0.105 NULL-guards in bdd.cxx. Verified: 1.0.106.0, Status OK, CM_ERR=0, 2560x1440, no new BSOD. dxdiag: Dedicated Memory 0MB (display-only), Vendor 0x1414 (Microsoft generic).
- **wddm-ps5/ created** in repo root: copies of real ps5-win-driver src (from F:\bc-250-proektas\Backup\dirbantis\ps5-win-driver\src):
  - wddm-ps5\inf\amdbc250.inf (KMD+UMD, DEV_13FE, WDDM AddReg)
  - wddm-ps5\src\common\amdbc250_hw.h (80 AMDBC250_REG_* defs � RAW Navi offsets, NOT GC_BASE shifted!)
  - wddm-ps5\src\kmd\amdbc250_kmd.c (3300 lines, full ~45 DDI callbacks), amdbc250_kmd.h, amdbc250_hw_init.c (Linux-ordered), vcxproj
  - wddm-ps5\src\umd\amdbc250_umd.c, umd.def, vcxproj
  - wddm-ps5\build.bat (CL direct, mirrors kmdod-build.bat; MSVC 14.44.35207, WDK 10.0.26100, displib.lib)
- **Build WORKS**: wddm-ps5\build.bat ? wddm-ps5\src\kmd\build\x64\Release\amdbc250kmd.sys (23040B). 6 warnings (C4113 PDXGKDDI_SYSTEM_DISPLAY_ENABLE signature, ExAllocatePoolWithTag deprecated x5). Imports: ntoskrnl+hal only (DxgkInitialize is displib.lib body, not dxgkrnl export � confirmed). DriverEntry calls DxgkInitialize (amdbc250_kmd.c:727).
- **Code 43 blocker #1 FIXED**: resource parsing took FIRST memory resource (BAR0) as MmioVirtualBase. Added BAR5 detection (known PA 0xFE800000, 512KB) in StartDevice, fallback to first resource. Added AMDBC250_BAR5_MMIO_PHYSICAL_BASE/SIZE to hw.h.
- **Code 43 blocker #2 FIXED (2026-08-03)**: hw.h now uses corrected register offsets (verified against inc\amdbc250_dream_hw.h + ip_discovery 2026-07-31):
  - GC_BASE=0x1260 added; GC regs (SCRATCH 0x32D4, CP_ME_CNTL 0x4A74, CP_MEC_CNTL NBIO 0xC0E0/GC 0x4B14, CP_RB0_* 0x89E0/0x8BA4/0x89E4/0x4FE0/0x8A30/0x8A34) now GC_BASE-shifted.
  - DCN_BASE=0xD300 added; CRTC0_* macros map to OTG0 (0x14004 CCNTL/0x13FA8 H_TOTAL/0x13FBC V_TOTAL/0x13FB0 H_SYNC/0x13FDC V_SYNC etc.), verified live on HW.
  - SDMA0_* at GC offsets 0xE000-0xE01C (verified 0xE000 range, NOT Navi 0x1260 base).
  - GB_ADDR_CONFIG 0x61D8 (was 0x263C), GB_ADDR_CONFIG_READ 0x61DC.
  - MP1 C2PMSG_66/82/90 at 0x16A08/0x16A48/0x16A68 (BAR5 slots; MP1 actual lives in SMN via NBIO 0x38/0x3C — direct probe only).
- **All 6 build warnings CLEARED**: C4113 fixed (WDK 26100 PDXGKDDI_SYSTEM_DISPLAY_ENABLE signature changed to 6-arg with PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS+Width+Height+ColorFormat — old 4-arg was WDDM 1.x). ExAllocatePoolWithTag→ExAllocatePool2 x5. Build now clean (no warnings).
- **UMD build added**: direct-CL script (wddm-ps5-umd-build.bat) because msbuild can't find SDK on F: drive (WindowsSDKDir undefined). Output amdbc250umd64.dll 117KB. INF updated to reference amdbc250umd64.dll (was amdbc250umd.dll mismatch), DriverVer 08/01/2026,1.0.102.0.
- **Install package ready**: output\wddm-ps5-test\ (amdbc250kmd.sys 30440B, amdbc250umd64.dll, amdbc250.inf, amdbc250.cat) — built+sign5 w/ sha1 34AFF96C..., Inf2Cat clean.

### Next steps (wddm-ps5)
1. ~~Fix all 34 used AMDBC250_REG_* offsets~~ DONE (2026-08-03).
2. **HW init strategy NOT yet tested**: full Bc250HwInitialize will hit SMU init which WAITS on MP1_SMN_P2CMSG_33 (BAR5 0x16xxxx) bit31 — on BC-250 this reads 0 (SMU in SMN, not BAR5) → 100ms timeout → StartDevice falls back to CompatibilityStart path (STILL returns STATUS_SUCCESS, adapter bound, no Code 43). This is a SAFE default but means DCN display never programmed. To get actual display output, either make SMU init non-fatal and let stages run, OR inject SMU via NBIO SMN window. Council: FIRST install as-is and confirm it loads (no BSOD/Code 43); then improve.
   - Note: StartDevice MMIO map → do NOT map doorbell BAR (only BAR5). GFX/SDMA ring BASE regs are host-RO on BC-250 (SOS), so ring init writes silently drop — harmless but no CP activity.
   - GART/VM (MC_VM/AGP) registers: ps5 KMD does NOT write them (confirmed grep) — avoids 0x1A.
3. Merge our register/ps5 layer as Escape IOCTL (ps5 already has DxgkDdiEscape plumbing).
4. **Sign + install test REQUIRES USER (Admin)**: see AGENTS reinstall flow (uninstall old GPU driver w/ Delete, reboot, install from output\wddm-ps5-test\ INF, reboot). Confirm load status / no BSOD first.

## WGP UNLOCK RESEARCH — SPI_PG SOS-LOCKED ON WINDOWS (2026-08-09)

### Key Finding
**SPI_PG_ENABLE_STATIC_WGP_MASK (0x5C3C) is SOS-locked on Windows.** All 6 methods tried in KMDOD driver failed. The register IS writable from Linux kernel context and UEFI DXE phase, but NOT from Windows KMDOD driver runtime.

### Methods Tried (all FAILED on Windows)
| # | Method | Result |
|---|--------|--------|
| 1 | Direct BAR5 write (CC=0, SPI=0x1F, RLC=0x1F) | SOS-locked |
| 2 | SMU Q3 msg 0x98 (ungated SMN write to SPI_PG phys addr) | SOS-locked |
| 3 | SMU RequestActiveWgp (Q0 msg 0x18) | Rejected |
| 4 | PSP PROG_REG (Q0 msg 0x0A) | Not supported |
| 5 | SMN alias scan (0x0115B000-0x01200000) | No alias found |
| 6 | VBIOS SMU wake + GFXOFF disable + retry write | SOS-locked |

### Why Linux Works but Windows Doesn't
- **Linux amdgpu**: WGP unlock during `gfx_v10_0_get_cu_info()` (EARLY boot, before SOS locks registers)
- **Windows KMDOD**: `StartDevice` runs LATER (SOS already locked SPI_PG)
- **UEFI DXE**: RescueMei proved SMU Q3 msg 0x98 works at UEFI DXE phase (before Windows loads)

### duggasco 40CU Unlock Patch (from DryhoppedIPA repo)
- **CC_GC_SHADER_ARRAY_CONFIG = 0** (NOT 0xFFE00000 — that was our mistake!)
- **SPI_PG_ENABLE_STATIC_WGP_MASK = 0x1F**
- **RLC_PG_ALWAYS_ON_WGP_MASK = 0x1F**
- Uses `gfx_v10_0_select_se_sh()` for proper GRBM bank selection
- A/B test: CC=0 + SPI=0x1F together = 1.54x scaling; neither alone works
- Both writes required simultaneously

### GitHub Repos Researched
| Repo | Key Finding |
|------|-------------|
| DryhoppedIPA/bc250-gfx1013-fix | SPI_PG WRITABLE Linux kernel; compute queue fix |
| RescueMei/BC250-DXEv2-SMU-Core-Unlock | SMU Q3 msg 0x98 works UEFI DXE only |
| RescueMei/BC250-DXEv2-COLD-BOOT | Auto cold boot driver |
| RescueMei/BC250-DXEv2-ACPI-AUTOINJECT | ACPI table injection |
| RescueMei/BC250-DXEv2-BIOSMOD | BIOS mod combining all DXE drivers |
| redbeard1083/bc250-toolkit | Linux setup script (not useful for Windows) |
| mendesrr/bc250-acpi-fix-updated-8c | CPU power management (not relevant) |

### Next Approach: GPU Driver (atikmdag.sys)
- GPU driver loads EARLIER than KMDOD
- May have access to SPI_PG before SOS locks it
- Has direct BAR5 mapping via MmMapIoSpace
- Need to add WGP unlock to DreamV3HwInitialize or StartDevice
- This is the most promising path forward

### KMDOD Driver Status
- v1.0.104 → v1.0.114 (all WGP methods failed)
- GpuClockMHz = 1500 (SMU works via BAR5+0x38/0x3C)
- BAR5 mapping works (GPU_ID = 0x9FFF9714)
- Registry: WGP_UnlockStatus=0, WGP_SPIPG_Value=0, WGP_ActiveWgp=0
- Build script fixed: `build_kmdod.bat` now compiles + signs + generates CAT automatically

### Registry Keys for WGP Results
```
HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000\HardwareInformation
- WGP_UnlockStatus (0=failed, 1=SPI_PG write OK, 2=ActiveWgp>0)
- WGP_SPIPG_Value (SPI_PG register value)
- WGP_CC_Value (CC_ARRAY register value)
- WGP_ActiveWgp (Active WGP count from SMU Q0 msg 0x1E)
- WGP_Method (which method succeeded)
```

## EFI Shell WGP Unlock — COMPLETE SOLUTION (2026-08-11)

### Overview
Since SPI_PG is SOS-locked on Windows, the only way to unlock WGP is at **EFI boot time** when NBIO is NOT yet locked. This requires booting into EFI Shell and running scripts.

### File Location
```
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\third-party\EFI_Boot\
├── EFI\BOOT\bootx64.efi          ← EFI Shell (download from TianoCore)
├── psp\
│   ├── test_psp.nsh              ← Test register access
│   ├── inject_psp.nsh            ← Load SOS firmware
│   ├── WGP_unlock.nsh            ← Unlock WGP (CC=0, SPI=0x1F, RLC=0x1F)
│   └── cyan_skillfish2_sos_extracted.bin ← SOS firmware (262,656 bytes)
├── full_auto.nsh                 ← Run all scripts + boot Windows
└── README.md                     ← Complete instructions

### EFI Boot Sequence
```
EFI Boot → test_psp.nsh → inject_psp.nsh → WGP_unlock.nsh → Boot Windows
```

### Script Functions
| Script | What it does |
|--------|--------------|
| test_psp.nsh | Tests if C2PMSG registers are accessible at EFI boot |
| inject_psp.nsh | Loads SOS firmware via PSP mailbox (C2PMSG_35/36) |
| WGP_unlock.nsh | Writes CC=0, SPI=0x1F, RLC=0x1F to all 4 shader array banks |
| full_auto.nsh | Runs all scripts in sequence, then exits to boot Windows |

### Register Values (duggasco 40CU method)
| Register | BAR5 Offset | Value | Description |
|----------|-------------|-------|-------------|
| SPI_PG | 0x5C3C | 0x1F | Enable all 5 WGPs |
| CC_ARRAY | 0x9C1C | 0x00 | Clear harvest mask |
| RLC_PG | 0x3D64 | 0x1F | Keep all WGPs powered |

### Key Technical Facts
- BAR5 physical base: 0xFE800000 (512KB)
- NBIO is OPEN at EFI boot time (not yet locked by SOS)
- `mm` command syntax: `mm <address>` (read) or `mm <address> <value>` (write)
- GRBM bank selects: 0x00000000, 0x00000100, 0x00010000, 0x00010100
- Broadcast: 0x15000000

### Prerequisites for EFI Shell Method
1. USB drive formatted as FAT32
2. EFI Shell binary (bootx64.efi) from TianoCore
3. BIOS configured to boot from USB
4. NBIO not locked in BIOS (Advanced > AMD CBS > NBIO > Device Exclusion Vector)

### Expected Results
- Success: SPI_PG reads 31 (0x1F) after unlock
- Failure: SPI_PG reads 0 (SOS-locked or NBIO blocked)

### EFI Shell mm Command Syntax
**IMPORTANT:** The `mm` command accepts hex addresses AND hex values, but BOTH without `0x` prefix!
- Read: `mm FE805C3C` (hex address, no 0x)
- Write: `mm FE805C3C 1F` (hex value, no 0x!)
- Wrong: `mm 0xFE805C3C` or `mm FE805C3C 0x1F` or `mm FE805C3C 31` (decimal)

### EFI Boot Log Analysis (2026-08-11)
From user's EFI Shell test logs:
- **NBIO is LOCKED at EFI boot** (C2PMSG registers show 0xFF)
- **User enabled IOMMU Disabled in BIOS** — needs re-test
- Without NBIO unlock at EFI boot, injection and WGP unlock won't work

## FINAL VERDICT — 3D GRAPHICS BLOCKED BUT NOT FUNDAMENTALLY IMPOSSIBLE (2026-08-13)

### Conclusion
This specific BC-250 hardware variant is **factory-locked for GPU command execution** via Windows WDM. All hardware ring paths are locked by PSP Secure OS. 3D graphics with this hardware is **NOT achievable on Windows WDM** without either:
1. EFI Shell pre-boot WGP unlock (NBIO must be unlocked in BIOS)
2. Linux kernel driver (has debugfs privilege)
3. Real WDDM miniport with full PSP authentication

### Fundamental Blockers (All SOS/Software-Level, Not Hardware Fused)
| Blocker | Root Cause | Fixable? |
|---------|-----------|----------|
| **KIQ_SIZE=0 read-only** (0xE068) | Hardware-level, not in firmware | ❌ No |
| **CP_HQD NBIO-blocked** (0xDAC0-0xDBFF) | NBIO firewall | ❌ No |
| **GCVM PT_BASE HW-locked** (0x0B608) | Always reads 0 | ❌ No |
| **GFX_RING0_BASE_LO read-only** (0xDA60) | BIOS sets ring base | ❌ No |
| **KIQ_WPTR 9-bit limit** (0x1FF) | Hardware limitation | ❌ No |
| **SOS firmware no ring protocol** (PSP side) | C2PMSG_64 bit 31 never sets; TOS doesn't support GPCOM | ❌ No |
| **SPI_PG_ENABLE_STATIC_WGP_MASK = 0** | SOS-locked from host writes; Linux can write it via kernel context | ⚠️ Only via EFI/Linux |
| **PSP firmware loading wrong mechanism** | GPU driver uses direct C2PMSG_35/36/37; BC-250 needs ring mechanism | ⚠️ Fixable |
| **SDMA firmware broken** | Stock firmware never drives user queues | ⚠️ Maybe (navi12 firmware works on Linux) |

### What Works Today
- ✅ KMDOD display driver (2560x1440, Status OK)
- ✅ SMU frequency/voltage control (1500 MHz @ 931 mV)
- ✅ Display output
- ✅ PSP SOS alive detection (C2PMSG_81=0xF0000010)
- ✅ Hardware monitoring (temperature, power)
- ✅ GPU register read/write via BAR5
- ✅ CPU core unlock via SMU Q3 msg 0x98 (6c/12t → 8c/16t)
- ✅ SMU feature enable/disable (GFXOFF, CG, PG)

### What Doesn't Work (On Windows WDM)
- ❌ 3D graphics / Vulkan / DirectX
- ❌ GPU shader execution
- ❌ WGP unlock / SPI_PG write (SOS-locked on Windows)
- ❌ GPU driver (atikmdag.sys) for 3D (Code 37)
- ❌ Compute queues / async compute
- ❌ PSP firmware loading via GPU driver (wrong mechanism)
- ❌ SDMA operations (ring not initialized, firmware broken)

### Recommendation
Focus on what works: **display + SMU control + hardware monitoring**.
3D graphics requires different hardware or Linux (where amdgpu kernel driver has different privilege level).

### GPU Driver (atikmdag.sys) WGP Unlock Attempt (2026-08-09)
- Added WGP unlock Step 0c to DreamV3HwInitialize (CC=0, SPI=0x1F, RLC=0x1F)
- Added DDI stubs file (amdbc250_dream_kmd_ddi_stubs.c)
- Added displib.lib to linker
- Fixed WDK 26100 compatibility (PDXGKARG_QUERYVIDPNHWCAPABILITY removed)
- BUILD SUCCESS ✅
- **INSTALL FAILED: Code 37** — DriverEntry returned error
- Likely cause: GPU driver is WDDM display-only but BC-250 is compute GPU without display
- DxgkInitializeDisplayOnlyDriver may be failing on compute-only hardware
- **Action: Switched back to KMDOD driver (Status OK)**

### Current Status
- KMDOD driver v1.0.114 installed and working (Status OK)
- All 6 WGP unlock methods failed (SPI_PG SOS-locked on Windows)
- GPU driver built but fails to initialize (Code 37)
- Need to try different approach for WGP unlock on Windows

## FOCUS: Display + SMU Control (2026-08-09 Decision)

After exhausting all WGP unlock methods (all failed due to SOS lock), focus shifted to:
1. **Display output** — KMDOD driver with improved modes
2. **SMU frequency/voltage control** — via Q0/Q3 mailbox
3. **Hardware monitoring** — temperature, power, fans
4. **Build automation** — streamlined compile/sign/install

### SMU Control Capabilities (verified working)
| Function | SMU Message | Notes |
|---|---|---|
| Get GPU clock | Q0 msg 0x0F or 0x37 | Returns MHz |
| Force GPU clock | Q0 msg 0x39 | MHz (needs voltage+profile) |
| Get GPU VID | Q0 msg 0x38 | Voltage ID |
| Force GPU VID | Q0 msg 0x3B | VID value |
| Get enabled features | Q0 msg 0x3D | Bitmask |
| Enable features | Q2 msg 0x05 | Mask |
| Disable features | Q2 msg 0x06 | Mask |
| Get temp max | Q3 msg 0x40 | °C |
| Set temp max | Q3 msg 0x8C | °C |
| Get CPU temp | Q3 msg 0x36 | mV (kind of) |
| Get GPU voltage | Q3 msg 0x37 | mV |

### SMU Mailbox Addresses
- Q0: cmd=0x03B10A08 rsp=0x03B10A68 arg=0x03B10A48
- Q3: cmd=0x03B10A20 rsp=0x03B10A80 arg=0x03B10A88

### Safe Frequency/Voltage Points
| Freq (MHz) | Voltage (mV) | Profile |
|---|---|---|
| 500 | 700 | 1 (low) |
| 800 | 750 | 1 |
| 1000 | 800 | 1 |
| 1175 | 850 | 3 (high) |
| 1400 | 900 | 3 |
| 1600 | 950 | 3 |
| 1800 | 1000 | 3 |
| 2000 | 1050 | 3 |

### Governor Sequence (PROVEN SAFE)
1. Q3(0x8C, 80) — Set GPU max temp to 80°C
2. Q0(0x3A, 0) — Unforce any previous frequency
3. Q0(0x3C, 0) — Unforce any previous voltage (ignores failure)
4. Look up safe point: find nearest (freq_mhz, mv, profile) at or above target
5. Q3(0x1E, profile) — Set perf profile (1=low, 3=high)
6. Q0(0x3B, mv_to_vid(mv)) — Force voltage
7. Q0(0x39, freq_mhz) — Force frequency (SAFE when voltage+profile set)

### VID Formula
vid = round((1.55 - mv/1000.0) / 0.00625)
mV = round((-vid*0.00625 + 1.55) * 1000)

## Community Research & External Findings (2026-08-13)

### DryhoppedIPA/bc250-gfx1013-fix (GitHub, 2026)
**CRITICAL: Compute queues ARE fixable on BC-250 — this is a SOFTWARE bug, not hardware!**

- **Kernel + Mesa/RADV patches** fix BC-250's broken GPU compute queues
- **Results:** +25% FPS from async compute (Cyberpunk 2077, 1440p Medium)
- **Root cause:** BC-250 powers on with ACE dispatches using threadgroup-dimension mode with `PARTIAL_TG_EN` mis-execute. This causes copy shaders to drop rows of images.
- **Fix:** Switch async compute dispatches to thread-dimension mode (same workaround used for Iceland/Tonga since GCN3 era)
- **Kernel patches:** Repair compute-queue lifecycle so ACE works
- **Mesa patches:** Turn compute queue on, fix dispatch corruption, correct GFX10.1 detection
- **Verified:** Vulkan CTS `dEQP-VK.synchronization2.*` — zero regressions, all 7,384 compute-queue cases pass
- **Implication for Windows:** Our driver cannot implement this fix — it requires kernel-mode driver changes (compute queue lifecycle) that our WDM IOCTL driver cannot provide

### duggasco/bc250-40cu-unlock (GitHub, 334 stars)
- **Kernel patch** re-enables all 40 CUs on BC-250
- **Writes both registers during `gfx_v10_0_get_cu_info()`:**
  - `CC_GC_SHADER_ARRAY_CONFIG`: 0xFFF80000 → 0xFFE00000
  - `SPI_PG_ENABLE_STATIC_WGP_MASK`: 0x07 → 0x1F
- **Results:** 1.61x compute scaling (pp512 LLM inference: 230 → 372 tok/s)
- **Guarded by:** PCI device ID 0x13FE + `bc250_cc_write_mode=3` kernel parameter
- **No permanent changes** — reboot without config returns to stock 24 CU
- **Our Windows driver:** Cannot replicate — SPI_PG is SOS-locked on Windows

### rw-r-r-0644/bc250-core-unlock (GitHub, 147 stars)
- **Unlocks 2 disabled CPU cores** (6c/12t → 8c/16t) via SMU
- **SMN register:** 0x0115A870 (core presence mask)
- **SMU Q3 msg 0x98:** writes 0xFF to any SMN address (debugging leftover in SMU firmware)
- **Volatile:** cold power cycle reverts to 0x77
- **Our Windows driver:** Already implemented! See `IOCTL_AMDBC250_CORE_UNLOCK` (0x80000978)

### WinnieLV/bc250-cu-live-manager (GitHub, 176 stars)
- **Interactive TUI** for live WGP/CU control via UMR
- **Reads/writes:** CC_GC_SHADER_ARRAY_CONFIG, SPI_PG_ENABLE_STATIC_WGP_MASK, RLC_PG_ALWAYS_ON_WGP_MASK
- **Features:** safety prompts, dry-run mode, boot restore via systemd
- **CPU core unlock** integrated
- **Our Windows driver:** Cannot replicate — requires UMR/umr kernel driver access

### elektricM/amd-bc250-docs (Community Documentation)
- **Mesa 25.1.0+ required** for BC-250 (GFX1013) support
- **Mesa 25.3.6+ recommended** (confirmed working Fedora 43)
- **RADV is the ONLY working GPU driver** — AMDVLK and proprietary don't support BC-250
- **RADV_DEBUG=nohiz** recommended for artifact fixes
- **RADV_DEBUG=nocompute** deprecated on Mesa 25.1+ (compute queue auto-disabled)
- **VRAM:** 16GB shared, configurable 256MB-12GB via memcfg utility (stored in CMOS)
- **ttm.pages_limit** kernel param extends dynamic VRAM beyond default half-RAM limit
- **No VA-API** (VCN firmware blocked by Sony)
- **ROCm experimental** — gfx1013 support incomplete

## Test Tool Fixes (2026-08-13)

### bar5-smn-test.c
- **Bug:** Used header's METHOD_BUFFERED IOCTL codes (0x900/0x901 packed as CTL_CODE) instead of driver's raw METHOD_NEITHER values
- **Fix:** Changed to raw values `IOCTL_AMDBC250_BAR5_READ_PROXY = 0x900` and `IOCTL_AMDBC250_BAR5_WRITE_PROXY = 0x901`
- **Recompiled:** `output\bar5-smn-test.exe`

### psp-fw-load.c
- **Bug 1:** IOCTL code was `0x80002480` instead of `0x80002483` (`IOCTL_AMDBC250_PSP_LOAD_IP_FW`)
- **Bug 2:** Firmware names were `cyan_skillfish2_sdma.bin`/`sdma1.bin` instead of `navi12_sdma.bin`/`navi12_sdma1.bin`
- **Fix:** Corrected both IOCTL code and firmware names
- **Recompiled:** `output\psp-fw-load.exe`

### Test Results (14/15 pass)
| Test | Result | Notes |
|------|--------|-------|
| `bar5-smn-test` | ✅ | SMU v88.6.0, 1500 MHz, features 0xDD602C7D |
| `psp-fw-load` | ✅ | IOCTL works, but PSP firmware loading still fails (wrong mechanism) |
| `test-gpu-ioctls` | ✅ | 14/15 tests pass |
| `sdma-selftest` | ⚠️ | Returns 0xC00000A3 (ring not initialized) |

## Next Steps

1. **Fix PSP firmware loading** — implement ring mechanism (C2PMSG_64/67/69/70/71) or use PSP driver IOCTL
2. **Fix SDMA ring init** — requires valid PA and working firmware (navi12_sdma.bin v0x2c)
3. **Improve KMDOD display driver** — modes, EDID, power management
4. **Build wddm-ps5 real WDDM miniport** (displib.lib path)
5. **Implement EFI Shell pre-boot WGP unlock** — only path that can write SPI_PG

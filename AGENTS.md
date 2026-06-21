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
- **Windows 11 26100**: Install GPU driver first (maps BAR5), then PSP driver (uses GPU proxy for mailbox access).
- **Older Windows**: PSP driver can map BAR5 directly; PSP driver can be installed before or after GPU driver.

## Architecture
- This is a WDM control/IOCTL driver, not a real WDDM miniport on Win11 26100; `DxgkInitialize` is not exported and the DDI path is stubbed.
- DriverBuildId registry marker confirms new binary loaded. Step markers: DriverEntryRan=1, Step_BeforeDxgkInit=10, Step_DriverEntryPost=11 (WDM fallback).
- Main IOCTL device: `\\.\AMDBC250DreamV43`; primary GPU MMIO BAR5 is `0xFE800000` (512KB).
- Do not map past BAR5 or probe random unknown offsets casually; hardware hangs require reboot.
- READ_REG/WRITE_REG use direct GPU BAR5 (`DreamV3ReadRegister`/`DreamV3WriteRegister`) when `g_Bar5Mapping` is available.
- On Windows 11 26100, if direct BAR5 mapping fails, READ_REG/WRITE_REG fall back to PSP proxy IOCTLs.
- PSP proxy is for PSP-specific work (mailbox, firmware loading) only.

## BC-250 hardware facts
- BC-250 GC registers are shifted by `GC_BASE=0x1260`; use `AMDBC250_REG_*` macros. Navi10 offsets like `0x2000`/`0x2004` read `0xFFFFFFFF` because they are unmapped, not NBIO-blocked.
- Key corrected offsets: GRBM `0x3260`, CC `0x3264`, scratch `0x32D4`, SPI WGP mask `0x34FC`.
- NBIO blocks writes in native `0xC000+` ranges such as `CP_ME_CNTL`/`CP_MEC_CNTL`; GC_BASE-shifted ring aliases can bypass NBIO but some BASE registers are hardware read-only.
- **Linux IP Base Map** (from `cyan_skillfish_ip_offset.h` — full details in `docs/BC250-LINUX-IP-MAP.md`):
  - GC: `0x1260` (+ `0xA000`), NBIO: `0x0000`, HDP: `0x0F20`, MMHUB: `0x1A000`
  - DF: `0x7000`, OSSSYS: `0x10A0`, MP0/PSP: `0x16000`, THM: `0x16600`
  - SMUIO: `0x16800`/`0x16A00`, CLK: `0x16C00+`, UMC0: `0x14000`, FUSE: `0x17400`
  - GC IP version: 10.1.3, NBIO: 2.1.1, MP0/PSP: 11.0.8
  - Linux skips PSP firmware loading entirely for BC-250 (`psp_v11_0_8.c` is minimal)
  - `cg_flags=0`, `pg_flags=0` (no clock/power gating), `external_rev_id = rev_id + 0x82`
- **40 CU unlock** (from duggasco/bc250-40cu-unlock): Write `CC_GC_SHADER_ARRAY_CONFIG` (0x3264) + `SPI_PG_ENABLE_STATIC_WGP_MASK` (0x34FC) together during `gfx_v10_0_get_cu_info()`

## Current command path and blockers
- KIQ submit from the GPU driver uses `PSP_IOCTL_KIQ_SUBMIT` (`0x818`) against the PSP driver.
- **PSP driver now programs GPU HQD registers** (PspKiq.c rewrite): KIQ_BASE, PQ_BASE, PQ_CONTROL, VMID, ACTIVE, WPTR.
- GPU driver SEND_PM4: PATH 1 (PSP KIQ) no longer requires GfxRing — only needs `HardwareInitialized`.
- GFX ring `CP_RING0_BASE_LO` (`0xDA60`) is read-only on current hardware.
- **FUNDAMENTAL BLOCKER**: SOS firmware (v3 navi10_sos.bin) does not support ring protocol (C2PMSG_64 bit 31 never sets).
- **NBIO_MAP INIT_HARDWARE works**: Driver fix (moved GPU alive test after NBIO_MAP break) resolved system hangs.
- CP registers at GC_BASE-shifted 0x3AD8-0x3AEC contain fence/doorbell addresses (0x02A8xxxx range).
- NBIO_ID at 0xC100 = 0xFEDCBAEF confirms NBIO accessible.
- SCRATCH (0x32D4) bit 31 is write-masked by hardware (W1C or read-only).

## GCVM (GPU VM) — Corrected offsets (verified 2026-06-18)
Formula: `BAR5_offset = GC_BASE(0x1260) + Linux_DWORD_offset * 4`
- **GCVM_CONTEXT0_CNTL = 0x0B460** — alive, writable (bit0=enable, bit1=DEFAULT_PAGE)
- **GCVM_CONTEXT0_PT_BASE_LO = 0x0B608** — alive but **HARDWARE LOCKED** (always reads 0)
- **GCVM_L2_CNTL = 0x0B360** — alive, value=0x013C67B8
- **DO NOT USE** 0x2840-0x2987 range or 0x1A00 range (all 0xFFFFFFFF, dead)

### PTE format (RDNA2/GFX10)
- PDE: bit0=VALID, bit1=SYSTEM → `0x03`
- PTE: bit0=VALID, bit1=SYSTEM, bit5=READABLE, bit6=WRITABLE → `0x63` (not 0x61!)
- Format from Linux `amdgpu_vm.h`: VALID=bit0, SYSTEM=bit1, SNOOPED=bit2, READABLE=bit5, WRITEABLE=bit6

### GCVM register writability map
**WARNING**: Writing 0xDEADBEEF to FULL_WRITABLE registers DESTROYS BIOS config! Always save/restore.
- **WRITABLE**: L2 TLB data 1-2 (0x0B320-0x0B324), Context0 regs (0x0B408-0x0B4AC), Context0 config (0x0B4C0-0x0B4D4 bits 0-7), GCVM_CONTEXT0_CNTL (0x0B460)
- **READ-ONLY**: L2 TLB tag (0x0B31C), L2 TLB data 3-15 (0x0B328-0x0B35C), L2_CNTL (0x0B360), Context0 base (0x0B404), PT_BASE (0x0B608-0x0B60C)
- **HARDWARE LOCKED**: PT_BASE_LO/HI (0x0B608-0x0B60C) — always 0, cannot configure page tables from OS
- BIOS config varies per boot; warm reboot doesn't fully reset GPU state.

## CP Ring registers
- **GFX RING0_BASE_LO (0xDA60)**: read-only (BIOS sets ring base)
- **COMPUTE_BASE_LO (0xDB60)**: read-only (same)
- **KIQ_BASE_LO (0xE060)**: writable → KIQ works
- All other ring registers (CNTL, RPTR, WPTR, DOORBELL) writable

## CP Firmware — Direct MMIO Load
- `LOAD_CP_FW` IOCTL (`0x80000BD4`): parses 44-byte firmware header, uploads Jump Table via UCODE_DATA registers.
- IC_BASE registers: ME_IC=0x17370-0x17378, PFP_IC=0x17360-0x17368, CE_IC=0x17380-0x17388
- Halt bits: ME_HALT=bit28, PFP_HALT=bit30, CE_HALT=bit29
- Firmware files in `firmware/` directory (cyan_skillfish2_me.bin, pfp.bin, ce.bin, mec.bin, rlc.bin, sdma.bin)
- IC_BASE DMA bypasses GCVM — firmware loads successfully via MMIO, but ring buffer access uses GCVM translation (which fails)

## PM4 submission — current blocker
- GPU CP cannot access ring buffer memory via GCVM translation.
- IC_BASE DMA works (firmware loads) → GPU CAN read system RAM for DMA, but ring buffer access uses different GCVM path.
- DEFAULT_PAGE bit (CONTEXT0_CNTL bit1) does not provide flat/physical addressing.
- Context0 TLB entries (0x0B408-0x0B4AC) are writable but format is unknown.

## PSP Proxy IOCTLs
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

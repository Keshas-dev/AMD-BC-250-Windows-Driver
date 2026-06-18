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
- **SCRATCH test**: W=0xDEADBEEF reads 0x5EADBEEF (bit 31 cleared), W=0x12345678 reads 0x12345678 OK.

## Progress
### Done
- ✅ GPU driver: BAR5 proxy IOCTLs (0x900 read, 0x901 write)
- ✅ PSP driver: GPU proxy fallback when `MmMapIoSpace` fails
- ✅ PSP mailbox working: `C2PMSG_81=0xF0000010`
- ✅ Firmware loading working: `LOAD_EMBEDDED_FW` succeeds
- ✅ KIQ submit working: `wptr` increments
- ✅ NBIO unlock working: `BOOT_SEQUENCE` sends SYSDRV/SOS
- ✅ All tests pass on Windows 11 26100
- ✅ v3 firmware (navi10_sos.bin) loaded via GPU proxy
- ✅ GRBM_STATUS accessible with v3 firmware (0x00000000)
- ✅ CREATE_RING implemented (allocates ring buffer, programs regs)
- ✅ NBIO_VIA_RING implemented (mailbox-based fallback) — **GRBM UNLOCKED via this path**
- ✅ INIT_HARDWARE NBIO_MAP fix: driver no longer hangs on init (moved GPU alive test after NBIO_MAP break)
- ✅ GC_BASE-shifted registers fully accessible: GPU_ID, GRBM_STATUS, CC_CONFIG, SCRATCH, SPI_WGP

### Blocked
- ❌ GPCOM ring: TOS protocol not in SOS firmware (`C2PMSG_64` bit 31 never sets)
- ❌ SDMA ring: Same ring protocol issue
- ❌ 3D rendering: No working command submission path
- ❌ GPU firmware: CP_ME/PFP/RLC not loaded — HQD queue can't activate without firmware running
- ❌ **PM4 submission**: GPU VM dead → GPU CP cannot access ring buffer memory
- ❌ **GPU VM (GCVM)**: All registers 0xFFFFFFFF (power-gated), cannot configure page tables
- ❌ **GRBM_GFX_INDEX queue select**: All 16 queues return identical values on BC-250
- ❌ **KIQ_CNTL (0xE068)**: Not writable through user-mode proxy

## GCVM (GPU VM) — ALIVE at corrected offsets (2026-06-18)

### Correct GCVM register offsets (BAR5 byte offsets)
Formula: `BAR5_offset = GC_BASE(0x1260) + Linux_DWORD_offset * 4`
- **GCVM_CONTEXT0_CNTL = 0x0B460** — ALIVE! value=0x010CA88D, WRITABLE (w=1 reads back 1)
- **GCVM_CONTEXT0_PT_BASE_LO = 0x0B608** — ALIVE! (was 0, writable)
- **GCVM_CONTEXT0_PT_BASE_HI = 0x0B60C** — ALIVE! (was 0, writable)
- **GCVM_L2_CNTL = 0x0B360** — ALIVE! value=0x013C67B8

### Previous WRONG offsets (DO NOT USE)
- 0x2840-0x2987 range = WRONG (all 0xFFFFFFFF)
- 0x1A00 range = WRONG (all dead)
- hw_extra 0x9B00-0x9B90 = readable as 0 but NOT writable

### GCVM page table setup (identity mapping)
- GPU_KIQ_TEST allocates PML4/PDP/PD/PT pages below 4GB
- Sets up identity mapping (VA=PA) for ring buffer page
- Programs GCVM_CONTEXT0_PT_BASE to PML4 physical address
- Enables GCVM context 0 (bit 0 of GCVM_CONTEXT0_CNTL)

### PTE format (RDNA2/GFX10) — CRITICAL FIX
- **PDE flags**: bit0=VALID, bit1=SYSTEM → `0x03` (correct)
- **PTE flags (FIXED)**: bit0=VALID, bit1=SYSTEM, bit5=READABLE, bit6=WRITABLE → `0x63`
- **OLD WRONG PTE**: `0x61` (missing SYSTEM bit!) → GPU tried to access system RAM as VRAM
- PTE format from Linux `amdgpu_vm.h`:
  ```
  AMDGPU_PTE_VALID    = bit 0
  AMDGPU_PTE_SYSTEM   = bit 1  (system memory, not VRAM)
  AMDGPU_PTE_SNOOPED  = bit 2  (CPU coherent)
  AMDGPU_PTE_READABLE = bit 5
  AMDGPU_PTE_WRITEABLE = bit 6
  ```

## HDP block alive (0x0500-0x05FF)
- 61 writable registers found
- MC_VM_FB_LOC_TOP (0x0524) and MC_VM_AGP_BASE (0x0528) writable

## CP Ring: RING0_BASE_LO is READONLY
- **GFX RING0_BASE_LO (0xDA60)**: readonly, value=0 → **BIOS sets ring base**
- **COMPUTE_BASE_LO (0xDB60)**: readonly, value=0 → same
- **KIQ_BASE_LO (0xE060)**: writable (0x46E00000) → KIQ works
- All other ring registers (CNTL, RPTR, WPTR, DOORBELL) writable

## CP Firmware — Direct MMIO Load (2026-06-18)
### LOAD_CP_FW IOCTL (0x80000BD4)
- Parses 44-byte firmware header: total_size, hdr_size, ucode_version, ucode_size, ucode_offset, jt_offset_dws, jt_size_dws
- Uploads Jump Table via UCODE_DATA registers (ME: 0x172B8/0x172BC, PFP: 0x172B0/0x172B4)
- IC_BASE registers: ME_IC=0x17370-0x17378, PFP_IC=0x17360-0x17368, CE_IC=0x17380-0x17388
- Halt bits: ME_HALT=bit28, PFP_HALT=bit30, CE_HALT=bit29

### Cyan Skillfish2 firmware files (from `firmware/` directory)
- `cyan_skillfish2_me.bin` (263,424 bytes) — ME ucode version=99, ucode_size=263168, JT=65536+128 DWs
- `cyan_skillfish2_pfp.bin` (263,424 bytes) — PFP ucode
- `cyan_skillfish2_ce.bin` (263,296 bytes) — CE ucode
- `cyan_skillfish2_mec.bin` (268,592 bytes) — MEC ucode
- `cyan_skillfish2_rlc.bin` (25,344 bytes) — RLC ucode
- `cyan_skillfish2_sdma.bin` (33,792 bytes) — SDMA ucode

## PM4 Submission Status (2026-06-18)
- GPU_KIQ_TEST programs KIQ/HQD, allocates ring buffer, sets up GCVM page tables
- **SCRATCH unchanged (0x4D585042)** — PM4 did not execute
- Root causes investigated:
  1. ~~GCVM dead~~ → FIXED: GCVM alive at corrected offsets
  2. ~~PTE flags wrong~~ → FIXED: 0x61→0x63 (added SYSTEM bit)
  3. **CP firmware not loaded** → NEXT: use LOAD_CP_FW to load ME+PFP before PM4
- HQD_ACTIVE was 0xFFF0 at boot (12 BIOS queues), GPU_KIQ_TEST cleanup destroys this
- **GRBM_GFX_INDEX queue select does not work** — all 16 queues return identical values

## PSP KIQ Path Status
- PSP driver `PspKiqCreateRing` allocates ring buffer
- `KIQ_SUBMIT` reports success (wptr increments) but GPU registers stay 0
- PSP proxy works for reads but GPU driver needs MMIO mapping

## GCVM Register Writability Map (2026-06-18)
**WARNING**: Writing 0xDEADBEEF to FULL_WRITABLE registers DESTROYS BIOS config! Always save/restore.
| Region | Offsets | Status | Notes |
|--------|---------|--------|-------|
| L2 TLB tag | 0x0B31C | **RO** | BIOS-cached tag data |
| L2 TLB data 1-2 | 0x0B320-0x0B324 | **WRITABLE** | Were BIOS values; destroyed by test |
| L2 TLB data 3-15 | 0x0B328-0x0B35C | **RO** | BIOS page translations |
| L2_CNTL | 0x0B360 | **RO** | Value 0x013C67B8, bit0=0 (L2 cache disabled?) |
| L2 config 1-3 | 0x0B364-0x0B36C | **RO** | Additional L2 config |
| Context0 base | 0x0B404 | **MASKED** | 0x00000CC5 (low bits writable) |
| Context0 regs | 0x0B408-0x0B4AC | **WRITABLE** | 39 regs, BIOS state DESTROYED by test |
| Context0 config | 0x0B4C0-0x0B4D4 | **MASKED** | Bits 0-7 writable |
| Invalidate tag | 0x0B51C | **RO** | |
| Invalidate data | 0x0B520-0x0B524 | **WRITABLE** | |
| Invalidate rest | 0x0B528-0x0B56C | **RO** | 17 DWORDs |
| PT_BASE_LO/HI | 0x0B608-0x0B60C | **RO** | HARDWARE LOCKED, always 0 |
| GCVM_CONTEXT0_CNTL | 0x0B460 | **WRITABLE** | bit0=1 enables L1 TLB |

### Key finding: Context0 regs (0x0B408-0x0B4AC) are WRITABLE but contain BIOS-configured state
- These could be **page table entries** or **identity mapping config**
- BIOS writes them at boot; we destroyed them with 0xDEADBEEF
- Need to read ALL values BEFORE overwriting, then decode format
- Some registers at 0x0B4C0-0x0B4D4 are MASKED (partial write protection)

## Key Next Steps
1. **PT_BASE is NOT writable** — BIOS hardware-locks GCVM_CONTEXT0_PT_BASE (0x0B608/0x0B60C). Cannot configure page tables from OS.
2. **GCVM L2 TLB has BIOS-cached translations** (0x0B31C-0x0B35C, 0x0B364-0x0B36C) — 19 DWORDs of encrypted/hardware-format entries. BIOS configures VM through L2 cache, not PT_BASE.
3. **MMHUB VM registers all ZERO** (0x1B400-0x1B600) — no MMHUB VM config
4. **BIOS doesn't use KIQ** (KIQ_BASE=0), uses GFX ring (RPTR=0x01200000, WPTR=0x00100010)
5. **CP firmware loads successfully** via LOAD_CP_FW ME ucode=0x63, PFP ucode=0x94
6. **ME_CNTL = 0xFFFBD9FB** on fresh boot — ME running, PFP halted
7. **Context0 regs (0x0B408-0x0B4AC) are WRITABLE** — decode format, try to configure identity mapping or add entries for ring buffer
8. **L2_CNTL bit0=0** — L2 cache may be disabled; enabling it might help

## PSP Proxy IOCTL Fix (2026-06-16)
- **ROOT CAUSE found**: PSP driver defined proxy IOCTLs as `CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900/0x901, ...)` = 0x00222400/0x00222405. GPU driver switch cases check raw values `case 0x900:` / `case 0x901:`. Different numbers → all proxy writes silently rejected.
- **FIX**: PSP driver `PspCore.c` now uses `#define IOCTL_AMDBC250_BAR5_READ_PROXY 0x900` / `#define IOCTL_AMDBC250_BAR5_WRITE_PROXY 0x901` (raw values).
- **Result**: PSP proxy writes now reach GPU hardware. GRBM_GFX_INDEX, CP_ME_CNTL, PERSISTENT_STATE writes confirmed working.
- **HQD registers**: Some are write-only (PQ_BASE, VMID, PQ_CONTROL) — can't read back. ACTIVE reads 0 even after writing 1 — queue won't activate because GPU ME/PFP firmware is not running.
- **Current blocker**: GPU firmware not loaded. KIQ queue activation requires ME/PFP microcode running. Need firmware load mechanism that doesn't depend on ring protocol.

## Windows 11 26100 BAR5 Proxy Support
- GPU driver maps BAR5 at `0xFE800000` via `INIT_HARDWARE` IOCTL
- PSP driver uses GPU proxy for mailbox access when `MmMapIoSpace` fails:
  - `IOCTL_AMDBC250_BAR5_READ_PROXY` (0x900): Read BAR5 via GPU driver
  - `IOCTL_AMDBC250_BAR5_WRITE_PROXY` (0x901): Write BAR5 via GPU driver
- Required: Install GPU driver first, then PSP driver

## Source and docs to trust
- Prefer `build.bat`, `src/kmd/amdbc250_dream_kmd.c`, `src/kmd/amdbc250_dream_hw_init.c`, `src/kmd/amdbc250_psp.c`, and `inc/amdbc250_dream_hw.h` over historical docs.
- Useful but potentially stale docs: `docs\REGISTER-MAP-BC250.md`, `docs\PSP-PROXY-BYPASS.md`, and `docs\RING-INIT-STATUS.md`.
- Trust: `docs\BC250-LINUX-IP-MAP.md` (Linux kernel source-verified IP base addresses).

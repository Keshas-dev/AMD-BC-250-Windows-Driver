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
- Helper script: `reinstall-both-drivers.bat` (run as Admin) uninstalls both old drivers, reboots, then auto-installs the new GPU + PSP drivers and reboots again.
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

## Progress Summary (2026-06-22)

### COMPLETED
- GPU driver rebuilt with `DreamV3WriteRegister` for BAR5 writes
- Fixed `KIQ_BIOS_RING_SUBMIT` input/output buffer sharing bug
- Added GCVM page table setup for 3-level and 4-level mappings
- PSP driver uses `AMD-BC-250-Signer`
- `bios-ring-test.exe` compiled and used for KIQ testing
- Verified register offsets: PT_BASE at Linux `0x6C8C` writable; `CTX0_CNTL` at `0x0B460` writable
- PSP driver has GPU BAR5 proxy support via raw IOCTLs `0x900` / `0x901`
- GPU driver accepts both raw `0x900` and CTL_CODE `0x0022020C` BAR5 proxy handlers
- `gpu-mmio-test` made safe by default with `--unsafe-writes` and `--deep-unsafe-reads` flags
- Updated `read-only-probe` with HQD poll address registers
- `RING-INIT-STATUS.md` updated with 2026-06-22 findings
- PSP driver rebuilt with KIQ HQD programming support
- PSP can read/write BAR5 directly (SCRATCH works, GRBM_STATUS=0)
- NBIO unlocked via boot sequence
- SOS firmware loaded via PSP mailbox
- CP firmware loading test tool created (`load-cp-fw.exe`)
- All three firmware types (ME, PFP, CE) load successfully

### KEY FINDINGS
- **GCVM PT_BASE (0x6C8C) is hardware-locked** - always reads 0, cannot configure page tables
- **HQD registers (0xC000+ range) are NBIO-blocked** - cannot program HQD via BAR5
- **HQD_PQ_WPTR_HI = 0x55555555** is stuck - test pattern, not writable
- **HQD_PQ_WPTR_POLL_ADDR = 0** - BIOS did NOT configure memory polling
- **KIQ_BASE reads 0x7E512000** (not expected 0x7E522000)
- **SCRATCH bit 31 is write-masked** - 0xDEADBEEF reads back as 0x5EADBEEF
- **SOS firmware doesn't support ring protocol** - C2PMSG_64 bit 31 never sets

### FUNDAMENTAL BLOCKERS
1. **Cannot set up GCVM page tables** - PT_BASE is hardware locked
2. **Cannot program HQD registers** - NBIO blocks 0xC000+ range
3. **No flat/physical addressing** - DEFAULT_PAGE doesn't help without working PT
4. **BIOS ring address mismatch** - KIQ_BASE reads wrong value

## Safety: register writes that HANG the GPU
- **DO NOT WRITE** `GRBM_GFX_INDEX` (0x34D0) — switching to non-existent ME/PIPE/QUEUE instance causes instant black screen (GPU TDR hang, reboot required).
- **DO NOT WRITE** unknown or unverified GCVM context registers — can corrupt BIOS GPU state config (some context regs destroy config if 0xDEADBEEF is written; always save/restore).
- **SAFE to write**: SCRATCH (0x32D4) — bit 31 is W1C/hardware-masked; lower 31 bits writable. Always test write patterns without bit 31 set first.
- **SAFE to read**: All register ranges mapped in BAR5. Reading `0xFFFFFFFF` means the offset is unmapped (dead region), not NBIO-blocked.

## Progress Summary (2026-06-23)

### COMPLETED
- First session end - detailed memory record saved
- comprehensive-pm4-test.exe verified stable - runs without hangs
- Confirmed: NBIO_MAP init (flag=1) works, full init (flag=0) HANGS (DreamV3HwInitialize)
- Confirmed: GRBM_GFX_INDEX (0x34D0) writes cause black screen hang
- Safe register scan created but NOT run (all 3 new tests hung system)
- ME_CNTL (0x4A74) goes from 0xFFFBD9FB (halted) to 0x00000000 (unhalted) after PSP KIQ operations
- KIQ_WPTR advances to 0x11 after submits, KIQ_RPTR stays at 0x00 (GCVM broken)
- PSP proxy reads low offsets OK (0x0000, 0x32D4, 0x4A74) but fails for high offsets (0xE060+)

### KEY FINDINGS
- Write to GRBM_GFX_INDEX = INSTANT BLACK SCREEN (confirmed 3x)
- DreamV3HwInitialize (12-step init) causes hang when called from user test (flag=0 path)
- NBIO_MAP init (flag=1) is the only safe init path - skips DreamV3HwInitialize
- ME_CNTL change is suspicious - PSP KIQ init/submit clears halt bits
- SCRATCH write bit 31 is hardware-masked (W1C) - safe to write lower 31 bits

### FILES MODIFIED
- `test-tools/gpu-mmio-test.c` - Added KIQ test support
- `test-tools/load-cp-fw.c` - Created firmware loader test
- `test-tools/comprehensive-pm4-test.c` - Main test suite (stable)
- `test-tools/safe-reg-scan.c` - Created but hung (NEVER RUN SAFELY)
- `test-tools/alt-path-test.c` - Created but hung (GRBM_GFX_INDEX write)
- `test-tools/safe-init-test.c` - Created but hung (flag=0, DreamV3HwInitialize)
- `test-tools/minimal-open-test.c` - Works, but IOCTL read fails without init
- `test-tools/gcvm-check-test.c` - GCVM config scanner (stable, detailed output)
- `test-tools/comprehensive-pm4-test.c` - Updated with PSP proxy test
- `C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\src\driver\PspDriver.c` - Fixed NBIO range check (narrowed 0xC000-0xFFFF -> 0xC000-0xC1FF)
- `AGENTS.md` - Updated with findings

## Session 2026-06-23 - PSP Proxy Fix + GCVM Analysis

### FIXED: PSP Proxy NBIO Range Check
**Bug**: PSP driver's `IOCTL_PSP_READ_REG` and `IOCTL_PSP_WRITE_REG` used `offset >= 0xC000 && offset < 0x10000` as NBIO range check. This caught ALL GPU ring/KIQ/HQD registers (0xDA60+, 0xE060+, 0xDAC0+) and redirected reads to PSP BAR0, which returned 0xFFFFFFFF for these non-NBIO registers.
**Fix**: Changed to `offset >= 0xC000 && offset < 0xC200` (PspDriver.c lines 395, 460). Verified: PSP proxy now returns correct values for all tested offsets.

### GCVM Analysis (from gcvm-check-test)
- **CONTEXT0_CNTL (0x0B460)** = 0x010CA88D: ENABLE=1, DEFAULT_PAGE=0, RETRY_FAULT=1
- **PT_BASE (0x0B608)** = 0x00000000 (hardware locked)
- **CTX0_PT_BASE0 (0x0B408)** = 0x7D9AB14E_017CCCC4 (non-zero but garbage/uninitialized)
- **System Aperture all zeros**: LOW=0, HIGH=0, DEFAULT=0
- **L2_CNTL (0x0B360)** = 0x013C7798: ENABLE_SYS_APERTURE bit=0 (read-only), MODE=3
- **TLB data**: All entries look like random/uninitialized data
- **Protection fault status**: Clean (no faults recorded)
- **L2_CNTL4 (0x0B36C)** = 0x6EB9E656 - garbage?

### KEY INSIGHT: GCVM Blocker is FUNDAMENTAL
- PT_BASE locked → cannot set up page tables
- System aperture disabled in read-only L2_CNTL → cannot enable identity mapping
- DEFAULT_PAGE=1 would help only if system aperture were enabled
- **TLB entries are garbage** → not even initialized by BIOS
- **KIQ ring access via CP uses GCVM translation** → cannot work without page tables

### KIQ Status After Fix
- PSP KIQ submit writes PM4 to ring buffer
- WPTR advances (0x00 → 0x11)  
- RPTR stays at 0 → GPU CP does NOT read from ring
- CP firmware IS loaded (ME, PFP, CE via IC_BASE DMA - uses physical addresses)
- But ring buffer access uses GCVM (broken)
- ME_CNTL change (halted → unhalted) comes from PSP KIQ init (step 16)

### Next Investigation Targets
1. **SDMA engine** - may use physical addresses directly (bypasses GCVM)
2. **CP direct MMIO commands** - write PM4 via CP_ME_RAM_DATA registers
3. **IC_BASE approach** - can IC_BASE be used to execute command buffers?

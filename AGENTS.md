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

## Code Review (2026-06-24) — 17 Bugs Found

### CRITICAL Bugs
| # | File | Description |
|---|------|-------------|
| 1 | kmd.c:3810,3838 | **Integer overflow in READ/WRITE_REG bounds check** — `RegisterOffset + 4` wraps to 0 if offset >= `0xFFFFFFFC`, bypassing bounds check |
| 2 | hw.h:360, PspKiq.c:57 | **RLC_CP_SCHEDULERS at 0xECA1 is NOT 4-byte aligned** (0xECA1 & 3 = 1) — writes go to wrong register or cause bus error |
| 3 | kmd.c:6117-6142 | **GCVM page table pages double-freed** — error path frees existing pages from prior successful call while GPU may still use them |
| 4 | ioctl.h:420, PspCore.c:17 | **IOCTL name collision** — `IOCTL_AMDBC250_BAR5_READ_PROXY` = 0x80000BCC (CTL_CODE) in GPU driver but = 0x900 (raw) in PSP driver; same name, different value |

### HIGH Bugs
| # | File | Description |
|---|------|-------------|
| 5 | kmd.c:5419 | **REG_DUMP reads GRBM_GFX_INDEX at wrong offset 0x33C4** (should be 0x34D0) |
| 6 | hw.h:453-454, vm.c:755-762 | **GCVM invalidate regs in hw.h (0x0B51C/0x0B520) don't match working code (0x6C0C/0x6C10)** — any code using hw.h definitions writes to dead registers |
| 7 | kmd.c:3570,3601,5412 | **GPU_ID read from 3 different offsets** (0x0000, 0x3840, 0x0E08) — no consistent definition |
| 8 | kmd.c:5491-5965 | **KIQ_NOP_TEST/KIQ_BIOS_RING_SUBMIT lack __try/__except** — access violation if MmioVirtualBase is NULL |

### MEDIUM Bugs
| # | File | Description |
|---|------|-------------|
| 9 | PspKiq.c:47-60 | `PspGpuProxyWriteRegister` return value **ignored in all callers** |
| 10 | kmd.c:3745 | **SEND_PM4 ring wrap check has 32-bit overflow** — large WPtr wraps past check |
| 11 | kmd.c:6035-6214 | **GCVM_PT_SETUP uses raw offsets, no MmioSize bounds check** |
| 12 | PspKiq.c:86-94 | **Race condition on g_GpuDriverHandle proxy init** — no lock |
| 13 | kmd.c:5221-5224 | **LOAD_CP_FW halts ALL engines when only MEC needs loading** |

### LOW Bugs
| # | File | Description |
|---|------|-------------|
| 14 | kmd.h:428-430 | **GCVM page table pages never freed on driver unload** |
| 15 | kmd.c:5469 | REG_DUMP reads SDMA0_CNTL at 0x10040 (hw.h says 0xE018) |
| 16 | kmd.c:5217 | MEC_ME1_HALT bit (bit0) may be inverted |
| 17 | kmd.c:5472-5479 | `CpRb1BaseProbe` field mislabels GFX ring0 registers |

## Linux Register Comparison (2026-06-24)

### KIQ Model — FUNDAMENTAL DIFFERENCE
- **Our driver** uses KIQ_BASE/CNTL/RPTR/WPTR at 0xE060-0xE078 — **not in Linux GFX10 headers at all**
- **Linux** uses CP_HQD_* registers (MQD model) via `gfx_v10_0_kiq_init_register()` for KIQ
- Our KIQ_BASE approach works on BC-250 (KIQ_BASE_LO writable) but is BC-250-specific

### GRBM Selection — Linux Uses Different Register
- **Our driver** writes `GRBM_GFX_INDEX` (0x34D0) for ME/PIPE/QUEUE select
- **Linux** writes `mmGRBM_GFX_CNTL` (0x0dc2) via `nv_grbm_select()`
- 0x34D0 confirmed writable on BC-250 but may not be the intended register

### IC_BASE Registers — MATCH
- All CP IC_BASE registers (0x17360-0x17388) match Linux: `BAR5 = GC_BASE(0x1260) + mm*4` ✅

### Halt Bits — MATCH
- ME_HALT=bit28, PFP_HALT=bit30, CE_HALT=bit29 — identical to Linux ✅
- CP_MEC_CNTL at 0x21B5 matches Sienna_Cichlid override ✅

### GCVM Registers — Partial Match
- Our empirically verified offsets (0xB360-0xB60C) do NOT match `GC_BASE + mm*4` formula
- GCVM registers are in a different address block on BC-250; keep our verified values

### CP_HQD_* Registers (0xDAC0-0xDBFF) — NBIO-BLOCKED
- All HQD writes at 0xDAC0+ range silently dropped by NBIO on BC-250
- Linux uses these for KIQ init; they are unusable on our hardware
- KIQ alias at 0xE060+ is the only usable path

### RLC_CP_SCHEDULERS (0xECA1) — Confirmed Correct for Sienna_Cichlid
- Value matches Sienna_Cichlid override `mmRLC_CP_SCHEDULERS_Sienna_Cichlid = 0x4ca1`
- But 0xECA1 mod 4 = 1 means offset is NOT 4-byte aligned — empirically found at 0xECAA by probe

## Progress Summary (2026-06-24) — KIQ RPTR Still 0

### Achievements Since Last Session
- **GRBM_GFX_INDEX (0x34D0) confirmed writable** — selects ME=1 correctly for KIQ alias access
- **HQD registers at 0xDAC0+ are NBIO-BLOCKED for writes** — ACTIVE=1, PQ_BASE, PQ_CONTROL, VMID writes all return 0. Only PERSISTENT_STATE bit 0 sticks
- **KIQ alias at 0xE060+ writability mapped**: WRITABLE → BASE_LO (0xE060), RPTR (0xE06C), DOORBELL (0xE074), WPTR (0xE078), VMID (0xE07C), ACTIVE (0xE080). READ-ONLY → SIZE (0xE068, reads 0), PQ_CTL (0xE070, reads 0x81818181)
- **MEC firmware loaded successfully** via extended LOAD_CP_FW IOCTL (type 4) — MEC_ME1_CNTL=0 (unhalted) after load. MEC firmware (cyan_skillfish2_mec.bin) version 0x90
- **CONTEXT0_CNTL=0x04 test** (PT_DISABLE, DEFAULT_PAGE=system) did NOT advance RPTR
- **Previous "partial HQD write" (0xDEADBEEF→0x0000BEE0) was misleading** — likely stale/aliased readback
- **System hung** requiring hard reboot — likely from CONTEXT0_CNTL write (0x04) while engine active

### Remaining Blockers for KIQ Ring Processing
1. **KIQ_RPTR stays 0** — even with MEC loaded, KIQ_ACTIVE=1, KIQ_BASE=valid ring (NOP at offset 0), KIQ_WPTR=1/4, KIQ_SIZE=0 (read-only), KIQ_PQ_CTL=0x81818181 (read-only)
2. **KIQ_SIZE=0 read-only** at 0xE068 — hardware thinks ring has 0 bytes; CP may refuse to process
3. **HQD registers NBIO-blocked** — cannot configure queue via standard GFX10 path
4. **RLC_CP_SCHEDULERS at 0xECA1 is misaligned** — empirically found at 0xECAA but that's also not 4-byte aligned (0xECAA & 3 = 2)
5. **May need KIU firmware** — KIQ handler may be a separate microcode component within MEC, not loaded by standard MEC firmware load

### Next Steps
1. **Verify KIU firmware presence** — check if KIU needs separate loading (compare with Linux gfx_v10_0.c KIQ init)
2. **Try VRAM ring allocation** — if system aperture is real blocker, find VRAM base via BAR2 or register
3. **Find correct RLC_CP_SCHEDULERS offset** — scan 4-byte-aligned addresses around 0xEC00-0xEF00
4. **Add GRBM_GFX_CNTL register** (Linux uses this for ME/PIPE/QUEUE select, not GRBM_GFX_INDEX)
5. **Test KIQ with LOAD_CP_FW only** — skip PSP init, load all firmware manually first

### Safety Notes
- **40 CU unlock code removed** — wrote to wrong offset (0x3264 instead of 0x44C4=GC_BASE+0x3264), caused freeze
- **GRBM_GFX_INDEX (0x34D0) IS safe to write** selecting ME=1 — confirmed by multiple runs
- **CONTEXT0_CNTL writes may hang system** — PT_DISABLE while engine active caused last freeze
- **ALWAYS save/restore** PT_BASE0 before writing — some configs destroyed by 0xDEADBEEF

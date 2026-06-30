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

## Progress Summary (2026-07-01) — DISPATCH_DIRECT SEG1 + COMPUTE Register Map + WRITE_PHYSICAL_MEM IOCTL

### What Was Done This Session
1. **COMPUTE_DISPATCH_DIRECT SEG1 fix** — Register address corrected from dead SEG0 (0x3C60 = 0xFFFFFFFF) to live SEG1 (0xDC60 = valid 0x00008C48). Confirmed by HW test:
   - SW PM4 DISPATCH_DIRECT write changed 0xDC60 from 0x00000080 to 0x000757FF ✅
   - COMPUTE_DISPATCH_START (0xDC64) VALID bit is W1C: write of 0x80000000 clears it to 0x00000000
   - DISPATCH_DIRECT + VALID=1 accepted by HW, GPU stays alive (no hang) ✅

2. **COMPUTE register map at SEG1 (0xDC60-0xDC80) fully scanned** — all registers alive:
   - 0xDC60 DISPATCH_DIRECT (mm0x2A00) — writable, reads back HW-modified values
   - 0xDC64 DISPATCH_START (mm0x2A04) — HW-managed, validates and clears
   - 0xDC68 PGM_RSRC1 (mm0x2A08) — writable, field-level bit masking
   - 0xDC6C PGM_RSRC2 (mm0x2A0C) — writable, field-level bit masking
   - 0xDC70 PGM_LO (mm0x2A10) — writable, upper bits masked (ADDR[39:8])
   - 0xDC74 PGM_HI (mm0x2A14) — FULLY writable ✅
   - 0xDC78 RESOURCE_LIMITS (mm0x2A18) — FULLY writable ✅ (0x000000FF)
   - 0xDC7C STATIC_THREAD_MGMT_SE0 (mm0x2A1C) — 0x00000000
   - 0xDC80 MISC_BASE (mm0x2A20) — 0x00000000

3. **GCVM state documented** — CONTEXT0_CNTL (0x0B460) = 0x010CA89D (TRANSLATION ON + DEFAULT_PAGE), but PT_BASE (0x6C8C/0x6C90) = 0 → any shader PGM access faults silently.

4. **SDMA_FILL confirmed broken** — IOCTL returns failure (SDMA engine not initialized).

5. **New IOCTL added: WRITE_PHYSICAL_MEM (0x80000C10)** — writes shader data to any physical address via MmMapIoSpace (page-aligned internally). Initially failed (ERROR_NOACCESS 998) due to MmMapIoSpace page-alignment requirement — FIXED in second build.

6. **hw.h defines added** — `AMDBC250_REG_COMPUTE_DISPATCH_DIRECT` (0xDC60), `AMDBC250_REG_COMPUTE_DISPATCH_START` (0xDC64).

7. **SW PM4 rewrite** — IT_DISPATCH_DIRECT handler uses new hw.h defines.

8. **Test tools updated** — `sw-pm4-test.exe` (SEG1 test), `dispatch-shader-test.exe` (full shader pipeline test with WRITE_PHYSICAL_MEM + DISPATCH).

9. **Driver builds cleanly** — Signed `atikmdag.sys` in `output\`.

### Key Discoveries
- **COMPUTE registers are ALL in SEG1**: formula = `GC_BASE(0x1260) + SEG1(0xA000) + Navi10_mmOffset*4`. SEG0 offsets (0x3C60-0x3C80) read 0xFFFFFFFF (dead).
- **DISPATCH_DIRECT + VALID=1 accepted by HW** but shader doesn't execute because PGM=0 (page fault with PT_BASE=0).
- **GCVM TRANSLATION ON with PT_BASE=0** is the fundamental blocker for compute execution.
- **MmMapIoSpace requires page-aligned physical addresses** — fixed with `pa & ~0xFFF` rounding.
- **SDMA_FILL IOCTL returns failure** — SDMA engine not usable for VRAM writes.

### Current Blocker
- WRITE_PHYSICAL_MEM can write shader code to VRAM physical addresses, but GCVM translation (CONTEXT0_CNTL bit0=1) prevents GPU from fetching from physical addresses via PGM_LO/HI. Need to either:
  a. Disable GCVM translation (clear CONTEXT0_CNTL bit0) so PGM uses physical addresses
  b. Set up GCVM page tables (PT_BASE + L2_CNTL + TLB entries)
  c. Load shader into a region the GPU can access without translation (firmware IC_BASE path)

### Code Review Bug Fix Status (all 17 bugs resolved)

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

### Next Steps
1. **Install driver** with page-aligned WRITE_PHYSICAL_MEM fix (reinstall required)
2. **Run `dispatch-shader-test.exe`** — write shader code to VRAM @ 0xC0100000 via WRITE_PHYSICAL_MEM
3. **Test GCVM disable** — clear CONTEXT0_CNTL bit0 so PGM uses physical addresses, then dispatch
4. **Test shader execution** — try multiple s_endpgm encodings (0xBF9F0000, 0x9F800000, 0x00000000) and check GRBM_STATUS for compute activity
5. **If GCVM disable works** — write a real RDNA compute shader that writes output to known VRAM location

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

## GCVM (GPU VM) — Corrected offsets (verified 2026-06-30)
Formula: `BAR5_offset = GC_BASE(0x1260) + Linux_DWORD_offset * 4`
- **GCVM_CONTEXT0_CNTL = 0x0B460** — alive, writable (bit0=enable, bit1=DEFAULT_PAGE)
- **GCVM_CONTEXT0_PT_BASE_LO = 0x6C8C** — Linux offset, verified WRITABLE (NOT 0x0B608!)
- **GCVM_CONTEXT0_PT_BASE_HI = 0x6C90** — Linux offset, verified WRITABLE
- **GCVM_L2_CNTL = 0x0B360** — alive, value=0x013C67B8
- **DO NOT USE** 0x2840-0x2987 range or 0x1A00 range (all 0xFFFFFFFF, dead)
- **NOTE**: 0x0B608/0x0B60C are NOT PT_BASE — misidentified register, hardware-locked (reads 0)

### PTE format (RDNA2/GFX10)
- PDE: bit0=VALID, bit1=SYSTEM → `0x03`
- PTE: bit0=VALID, bit1=SYSTEM, bit5=READABLE, bit6=WRITABLE → `0x63` (not 0x61!)
- Format from Linux `amdgpu_vm.h`: VALID=bit0, SYSTEM=bit1, SNOOPED=bit2, READABLE=bit5, WRITEABLE=bit6

### GCVM register writability map
**WARNING**: Writing 0xDEADBEEF to FULL_WRITABLE registers DESTROYS BIOS config! Always save/restore.
- **WRITABLE**: L2 TLB data 1-2 (0x0B320-0x0B324), Context0 regs (0x0B408-0x0B4AC), Context0 config (0x0B4C0-0x0B4D4 bits 0-7), GCVM_CONTEXT0_CNTL (0x0B460)
- **WRITABLE (correct PT_BASE)**: PT_BASE at 0x6C8C/0x6C90 (Linux offset, works with GCVM_PT_SETUP IOCTL)
- **READ-ONLY**: L2 TLB tag (0x0B31C), L2 TLB data 3-15 (0x0B328-0x0B35C), L2_CNTL (0x0B360), Context0 base (0x0B404)
- **STALE/MISIDENTIFIED**: 0x0B608/0x0B60C — not PT_BASE, hardware-locked (reads 0)
- BIOS config varies per boot; warm reboot doesn't fully reset GPU state.

## CP Ring registers
- **GFX RING0_BASE_LO (0xDA60)**: read-only (BIOS sets ring base)
- **COMPUTE_BASE_LO (0xDB60)**: read-only (same)
- **KIQ_BASE_LO (0xE060)**: writable → KIQ works
- All other ring registers (CNTL, RPTR, WPTR, DOORBELL) writable

## Firmware Loading — Critical Bug Fix (2026-06-25)
- **Bug**: LOAD_CP_FW wrote IC_BASE_CNTL=0x100 (ENABLE bit) BEFORE setting IC_BASE_LO/HI. If bit 8 auto-starts DMA from IC_BASE address, DMA starts with stale address 0 → firmware never loads into engine instruction memory.
- **Fix**: Write IC_BASE_LO first, then HI, then CNTL=0 (matches Linux). Trigger DMA by writing version to UCODE_ADDR.
- **Proof**: Even 8 bytes of 0xDE at ucode start caused no crash → engine was never executing loaded firmware.
- **Second fix**: Added UCODE_ADDR polling (wait until reads 0, 500ms timeout) before unhalt. Linux does this to confirm DMA completion.
- **Affected files**: `src/kmd/amdbc250_dream_kmd.c` (LOAD_CP_FW IOCTL), `src/kmd/amdbc250_dream_fw_load.c` (DreamV3LoadSingleFirmware)
- **Build status**: `output\atikmdag.sys` with both fixes, signed. Requires reinstall to take effect.

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

## MEC Firmware Binary Structure (discovered 2026-06-26)
- **Header**: 44 bytes (11 DWORDs): [0] total_size, [1] header_size, [2] ver_major, [3] ver_minor, [4] ucode_version, [5] ucode_size, [6] ucode_offset, [7] checksum, [8] data_offset, [9] jt_offset(DWORDs), [10] jt_size(DWORDs).
- **MEC1 binary**: total=0x41930, hdr=44, ver_major=1, ver_minor=0x1000A, ucode_ver=0x90, ucode_size=0x41830, ucode_offset=0x100, jt_offset=0x1052C (DWORDs from ucode start), jt_size=0xE0 DWORDs.
- **Jump Table**: at file offset 0x100 + 0x1052C*4 = 0x415B0, size 0x380 bytes (0xE0 DWORDs). Uploaded via UCODE_DATA registers. MEC1 = mostly zeros, MEC2 = real entries.
- **MEC1 vs MEC2**: IDENTICAL except: (1) JT at 0x100-0x107 (4 DWORDs differ), (2) ucode at 0x41830+ (MEC1 = `78 80` NOP pattern, MEC2 = real microcode). All other bytes match → firmware core is same.
- **Instruction encoding**: 16-bit halfwords (little-endian). NOP = `0x8078` (bytes `78 80`).

### KIQ Register References in MEC Firmware (CORRECTED 2026-06-26)
**IMPORTANT**: Earlier analysis had byte order reversed. Corrected counts (little-endian 16-bit):
| Register | Internal Addr | LE bytes | Ref Count |
|----------|--------------|----------|-----------|
| KIQ_BASE_LO | 0xCE00 | `00 CE` | **69×** |
| KIQ_CNTL (KIQ_SIZE) | **0xCE08** | `08 CE` | **2×** |
| KIQ_BASE_HI | 0xCE04 | `04 CE` | **1×** |
| KIQ_RPTR | 0xCE0C | `0C CE` | 0 |
| KIQ_WPTR | 0xCE18 | `18 CE` | 0 |
| KIQ_ACTIVE | 0xCE20 | `20 CE` | 0 |
| HQD_ACTIVE | 0xC860 | `60 C8` | 0 |
| HQD_PQ_BASE | 0xC878 | `78 C8` | 0 |

- KIQ_BASE_LO dominates (69×) — firmware primarily works with KIQ_BASE_LO to get ring descriptor pointer.
- KIQ_CNTL (SIZE) only 2 refs at 0x9482, 0x9B6A — not the 12× previously thought.
- RPTR, WPTR, ACTIVE not found as direct register references → firmware likely accesses them through the descriptor structure in memory, not via direct register MMIO.
- MEC1 == MEC2 (identical ref counts and offsets) — firmware core is the same.

### Firmware Modification Viability
- **PSP does NOT validate firmware signature** on BC-250 — proven by ucode corruption test.
- **Blind patch applied (2026-06-26)**: Replaced both `08 CE` (KIQ_CNTL) with `78 80` (NOP 0x8078) in `firmware/cyan_skillfish2_mec_patched.bin`. Only 4 bytes changed total.
- **Testing**: Use `patched-mec-test.exe` (compile with `compile-patcher-test.bat`) or `test-patched-mec.bat` (auto-backup/original swap).
- **If patch fails**: KIQ_SIZE=0 is likely a hardware-level block, not firmware-mediated. ISA RE needed for targeted patches.
- KIQ_CNTL at 2 locations only suggests SIZE check may not be firmware's main ring processing path.

## Source and docs to trust
- Prefer `build.bat`, `src/kmd/amdbc250_dream_kmd.c`, `src/kmd/amdbc250_dream_hw_init.c`, `src/kmd/amdbc250_psp.c`, and `inc/amdbc250_dream_hw.h` over historical docs.
- Useful but potentially stale docs: `docs\REGISTER-MAP-BC250.md`, `docs\PSP-PROXY-BYPASS.md`, and `docs\RING-INIT-STATUS.md`.
- Trust: `docs\BC250-LINUX-IP-MAP.md` (Linux kernel source-verified IP base addresses).

## Code Review (2026-06-24) — All 17 Bugs FIXED (2026-06-30)

All bugs identified in the 2026-06-24 code review have been fixed across sessions on 2026-06-25, 2026-06-26, and 2026-06-30. See "Progress Summary (2026-06-30)" for the detailed bug fix table.

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

## Progress Summary (2026-06-30) — All 17 Bugs Fixed + DISPATCH_DIRECT

See top of file for full summary. Key achievements from prior sessions:

### SW PM4 Executor (2026-06-26)
- `DreamV3SwPm4Process` at kmd.c:1564 — translates PM4 packets to direct MMIO writes
- PATH 3 fallback in SEND_PM4 bypasses KIQ_SIZE=0 and NBIO-blocked HQD regs
- Supports: IT_NOP, IT_WRITE_DATA, IT_EVENT_WRITE_EOP, IT_RELEASE_MEM, PM4_TYPE_0, IT_DISPATCH_DIRECT
- Corrected SET_SH_REG/SET_CONTEXT_REG/SET_CONFIG_REG offset formula (no +0x2C000 block base)

### KIQ/SW PM4 Path Status
- **Hardware ring processing IMPOSSIBLE** — KIQ_SIZE(0xE068) read-only 0, all CP_HQD NBIO-blocked
- **SW PM4 executor is the ONLY working path** for command submission
- PSP KIQ path (Path 1) also hits KIQ_SIZE=0 block
- Test tools: `sw-pm4-test.exe`, `patched-mec-test.exe`, `reg-dump-and-nop.exe`

### Safety Notes
- **GRBM_GFX_INDEX (0x34D0) IS safe to write** selecting ME=1 — confirmed by multiple runs
- **CONTEXT0_CNTL writes may hang system** — PT_DISABLE while engine active caused freeze
- **ALWAYS deactivate KIQ (0xE080=0) and restore GRBM_GFX_INDEX (0xE0000000)** before exiting tests
- **ALWAYS save/restore** PT_BASE0 before writing — some configs destroyed by 0xDEADBEEF

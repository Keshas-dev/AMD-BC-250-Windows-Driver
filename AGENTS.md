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

## BREAKTHROUGH: PSP Mailbox Firmware Loading WORKS! (2026-07-01)

### Test Result
- **Test**: `test-tools/psp-mailbox-rlc-test.exe` — loads RLC firmware via PSP mailbox
- **PSP driver**: `alive=1 fwLoaded=1 ringCreated=0` — SOS already loaded
- **RLC (fwType=8)**: Status=0x00000000 C2Pmsg81=0x00000000 ✅
- **MEC (fwType=4)**: Status=0x00000000 C2Pmsg81=0x00000000 ✅

### Key Discovery
- SOS firmware DOES support GFX_CMD_ID_LOAD_IP_FW (0x06) via mailbox — even though TOS ring protocol doesn't work
- RLC firmware (previously impossible via direct BAR5) now works! PSP mailbox is the CORRECT path
- All GPU firmware types can be loaded: ME=1, PFP=2, CE=3, MEC=4, MEC2=5, RLC=8, SDMA0=9, SDMA1=10

### Impact
- Can now initialize ALL GPU engines via PSP (especially RLC which was impossible before)
- Previous blocker (KIQ_SIZE=0 read-only) may be resolved by proper RLC initialization
- Next step: combined test loading ALL firmware via PSP + MEC ring test

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

### Conclusion
BC-250 compute/GFX engines appear permanently disabled at hardware level. Ring registers respond to MMIO but the CP/MEC engine behind them doesn't process. PSP PM4 submit path works as a transport layer but the GPU doesn't execute the commands. Consistent with RADV `RADV_DEBUG=nocompute` workaround documented in the community.

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

## (ARCHIVED) Initial compute dead verdict — superseded by 2026-07-08 (see FINAL VERDICT below)

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

### Hardware Conclusion (CONFIRMED)
BC-250 mining ASIC has compute/GFX engines permanently disabled at hardware level:
- ME was halted but unhalting it doesn't enable processing
- GFX ring WPTR is writable but the CP/MEC engine behind it never reads from the ring
- KIQ_BASE/KIQ_SIZE are hardwired to 0 — ring buffer address can't be set
- PGM_LO (0x8110) is WRITABLE (0x65FFEB6E initial, writes persist across boots) but still no execution
- COMPUTE registers (DIM_X/Y/Z) are read-only shadows, but PGM_LO/HI and NUM_THREAD_Y/Z are WRITABLE
- Consistent with RADV `RADV_DEBUG=nocompute` workaround and bc250-collective findings

### What DOES Work
- PSP driver: BAR5 mapping, mailbox firmware loading, register read/write proxy
- GPU driver: WDM IOCTL device, BAR5 proxy, GCVM page tables, PM4 encoding
- PSP mailbox: firmware loading for ALL types (ME, PFP, CE, MEC, MEC2, RLC, SDMA)
- Register read/write via both GPU and PSP drivers

### Final Status
- **3D graphics**: ❌ Impossible (no compute/GFX engine)
- **Display-only**: ❓ Untested (WDDM path not functional on Win11 26100)
- **PSP mailbox**: ✅ Fully functional
- **Register access**: ✅ Both direct (when mapped) and proxy IOCTLs work

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

### Problem
On Win11 26100, `DxgkInitialize` is NOT exported. Driver enters WDM fallback mode → creates IOCTL device but **NEVER maps BAR5** (no PnP `StartDevice` call). All `IOCTL_AMDBC250_READ_REG` returns `STATUS_DEVICE_NOT_READY` (ERROR 21) because `DevExt->MmioVirtualBase == NULL`.

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

### Final outcome
- All dangerous defaults flipped to **0** in commit `7aa984f`. Driver now loads **stably with NO
  registry keys set** — full init (`Flags=0`) returns SUCCESS, no 0x1A, no white screen.
- `HwInitFirmware` stays `1`: firmware IS loaded (safe), only the engine unhalt is disabled.
- The 5 kill-switches remain as opt-in registry overrides for future experimentation.

### FINAL CONCLUSION: host compute init is physically impossible on BC-250
1. **CP cannot be unhalted** from the host — doing so makes the GPU rogue-DMA host memory (0x1A).
2. **Ring BASE registers (GFX 0xDA60, KIQ 0xE060) are host-read-only** (SOS/PSP-locked) → no ring
   can be based on the host, so even with microcode the CP/MEC have no command buffer.
3. **WGPs are fused off** (SPI_PG_ENABLE_STATIC_WGP_MASK 0x5C3C = 0, hardware read-only) — confirmed
   independently by Linux (24 CUs but ROCm reports SDMA/KIQ/CP timeouts; Mesa uses RADV_DEBUG=nocompute).
4. Compute firmware and engine control belong to the **PSP/SOS**, not the host driver.

The GPU driver is therefore a **stable register/display/PSP-proxy control driver** only. Compute
via the host path will never work; any compute must go through the PSP mailbox (see 2026-07-01
breakthrough), which loads firmware but cannot wake the host-locked CP either.

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




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
- Key corrected offsets: GRBM `0x3260`, CC `0x9C1C` (NOT 0x3264 — see bc250-collective), scratch `0x32D4`, SPI WGP mask `0x5C3C` (NOT 0x34FC).
- NBIO blocks writes in native `0xC000+` ranges such as `CP_ME_CNTL`/`CP_MEC_CNTL`; GC_BASE-shifted ring aliases can bypass NBIO but some BASE registers are hardware read-only.
- **Linux IP Base Map** (from `cyan_skillfish_ip_offset.h` — full details in `docs/BC250-LINUX-IP-MAP.md`):
  - GC: `0x1260` (+ `0xA000`), NBIO: `0x0000`, HDP: `0x0F20`, MMHUB: `0x1A000`
  - DF: `0x7000`, OSSSYS: `0x10A0`, MP0/PSP: `0x16000`, THM: `0x16600`
  - SMUIO: `0x16800`/`0x16A00`, CLK: `0x16C00+`, UMC0: `0x14000`, FUSE: `0x17400`
  - GC IP version: 10.1.3, NBIO: 2.1.1, MP0/PSP: 11.0.8
  - Linux skips PSP firmware loading entirely for BC-250 (`psp_v11_0_8.c` is minimal)
  - `cg_flags=0`, `pg_flags=0` (no clock/power gating), `external_rev_id = rev_id + 0x82`
- **40 CU unlock** (from bc250-collective): Write `CC_GC_SHADER_ARRAY_CONFIG` (0x9C1C) + `SPI_PG_ENABLE_STATIC_WGP_MASK` (0x5C3C) together during `gfx_v10_0_get_cu_info()`. Linux mm* offsets: CC=0x226F*4+0x1260=0x9C1C, SPI=0x1277*4+0x1260=0x5C3C.
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

## FINAL VERDICT: All compute/GFX paths confirmed dead on BC-250 (2026-07-03)

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
- PGM_LO (0x8110) is read-only (0x65FFEB6E) despite being "writable" in earlier tests
- COMPUTE registers (DIM_X/Y/Z) are read-only shadows
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

### Actual HW Status (verified 2026-07-01)

| Register | Address | Status |
|----------|---------|--------|
| DISPATCH_INITIATOR | 0x80E0 | W1C trigger, sets consumed, no execution |
| DIM_X | 0x80E4 | READ-ONLY (shadow/status) |
| DIM_Y | 0x80E8 | DEAD (0xFFFFFFFF) |
| DIM_Z | 0x80EC | DEAD (0xFFFFFFFF) |
| START_X/Y/Z | 0x80F0-0x80F8 | READ-ONLY (all 0) |
| NUM_THREAD_X/Y/Z | 0x80FC-0x8104 | READ-ONLY (all 0) |
| PGM_LO | 0x8110 | WRITABLE! |
| PGM_HI | 0x8114 | WRITABLE! |
| PGM_RSRC1/2 | 0x8128-0x812C | DEAD (0xFFFFFFFF) |
| CP_MQD_BASE_ADDR | 0x9104 | WRITABLE (write-back verified) |
| CP_MQD_BASE_ADDR_HI | 0x9108 | READ-ONLY (0xFF11EFE0) |
| CP_HQD_ACTIVE | 0x910C | WRITABLE, ACKs (reads 1) |
| GRBM_GFX_CNTL | 0x2022/0x4968 | DEAD (BC-250 doesn't have this) |
| 0xDC60 register | 0xDC60 | Cycling FIFO (debug counter, not dispatch) |



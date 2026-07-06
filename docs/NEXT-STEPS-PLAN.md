# AMD BC-250 Windows Driver — Next Steps Plan

## Status: All Known Bugs Fixed ✅ (2026-07-06)
- PSP driver: 7 bugs fixed (RPTR mask, GRBM_GFX_INDEX, PspKiqCleanup race, SMU SMN path, OOB cmdCount, __try/__except, PspDoBootSequence double-free)
- GPU driver: 3 bugs fixed (g_PciDevExt leak, Amdbc250PspReadRegister 0xFFFFFFFF, METHOD_BUFFERED warning)
- Test tools: 3 offset fixes (ME_CNTL, GRBM_STATUS)
- Both drivers reinstalled and signed

## Conclusion: GPU is NOT hardware-fused. Linux proves it works.

Linux amdgpu runs compute on BC-250 (24 CUs, SMU v88.6.0, all firmware loaded).
Our driver is missing ~4 critical initialization phases:

---

## Phase 1 (Immediate): SMU DPM Initialization

**Problem**: GFX stays in deep sleep because SMU has no DPM configuration.
SMU v88.6.0 is alive but has no PowerPlayTable → GFXOFF keeps clock at 15 MHz.

**What Linux does:**
1. Allocate DriverPPTable in VRAM/system memory
2. msg 0x4 + 0x5 (SetDriverTableDramAddrHigh/Low) — tell SMU where table is
3. msg 0x7 (TransferTableDram2Smu) param=0 — copy table into SMU firmware
4. msg 0x35 (SetSoftMinCclk) param=20000 (200 MHz)
5. msg 0x36 (SetSoftMaxCclk) param=40000 (400 MHz)

**Our implementation:**
- Write to `C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd\amdbc250_dream_power.c`
- New function: `DreamV3SmuInitDpm()`
- Use SMN path via NBIO 0x38/0x3C for SMU communication
- SMU messages from `smu_v11_8_ppsmc.h` (PPSMC_MSG_* IDs)

**PPTable source needed**: The PPTable binary must be extracted from:
- `cyan_skillfish2_smc.bin` firmware file (embedded in firmware header v2.0/v2.1)
- Or from a working Linux system: `cat /sys/kernel/debug/dri/0/amdgpu_pm_info`
- Or from Linux source: `smu11_driver_if_cyan_skillfish.h` has struct definition

---

## Phase 2: MQD-Based KIQ/HQD Ring Init

**Problem**: Current driver uses KIQ_BASE_LO (0xE060) registers directly, which is the WRONG approach. Linux uses MQD (Memory Queue Descriptor) in system memory + CP_HQD registers.

**What Linux does:**
1. Allocate MQD (768 bytes) in GTT system memory — struct `v10_compute_mqd`
2. Fill MQD fields:
   - `header = 0xC0310800`
   - `cp_hqd_persistent_state = 0x8000014C` (PRELOAD_REQ + 0x53 preload size)
   - `cp_hqd_pq_control = QUEUE_SIZE | RPTR_BLOCK_SIZE=5 | UNORD_DISPATCH`
   - `cp_hqd_pq_base_lo = ring_addr >> 8` (Qword-aligned)
   - `cp_mqd_base_addr_lo = mqd_addr` (self-pointer!)
   - `cp_hqd_active = 1`
3. Program CP_HQD registers via MMIO (in order):
   - GRBM_GFX_INDEX = ME=1 (0x00010000)
   - CP_HQD_ACTIVE = 0
   - CP_PQ_WPTR_POLL_CNTL = 0
   - CP_HQD_EOP_BASE_ADDR = eop_buf >> 8
   - CP_HQD_EOP_CONTROL = 0x08000000 (EOP size)
   - CP_MQD_BASE_ADDR = mqd_addr
   - CP_HQD_PQ_BASE = ring_addr >> 8
   - CP_HQD_PQ_CONTROL = mqd->cp_hqd_pq_control
   - CP_HQD_PERSISTENT_STATE = 0x8000014C
   - CP_HQD_ACTIVE = 1
4. Send PM4 via KIQ ring:
   - SET_RESOURCES (8 DWORDs) — tells MEC which queues KIQ owns
   - MAP_QUEUES (7 DWORDs) — ENGINE_SEL=1 (HIQ for KIQ!)
   - Then kick WPTR

**Critical differences from current code:**
- CP_HQD_PQ_BASE (0x9124) instead of KIQ_BASE_LO (0xE060) — registers at 0xE060 are NOT used by Linux!
- Ring addr >> 8 (not raw address)
- MQD in memory with self-pointer at CP_MQD_BASE_ADDR
- PM4 MAP_QUEUES tells MEC to read the MQD

**Implementation files:**
- New: `src/kmd/amdbc250_dream_mqd.c` — MQD struct definition, allocation, filling
- Modify: `src/kmd/amdbc250_dream_hw_init.c` — replace KIQ init with MQD-based init
- Modify: `src/kmd/amdbc250_dream_kmd.c` — PM4 SET_RESOURCES/MAP_QUEUES

---

## Phase 3: CPU Frequency/Voltage Control

**Problem**: SMU can control CPU and GPU frequencies, but we never configure voltage/frequency tables.

**Key SMU messages from smu_v11_8_ppsmc.h:**
- 0x0E (RequestGfxclk) — request specific GFX clock
- 0x0F (QueryGfxclk) — query current GFX clock
- 0x1E (QueryActiveWgp) — query active WG count
- 0x2C (SetCoreEnableMask) — enable/disable cores
- 0x35 (SetSoftMinCclk) — set min clock (200 MHz safe)
- 0x36 (SetSoftMaxCclk) — set max clock (400 MHz safe)
- 0x37 (GetGfxFrequency) — read current GFX freq
- 0x38 (GetGfxVid) — read GFX voltage
- 0x39 (ForceGfxFreq) — **DANGER! Causes crash without DPM tables!**
- 0x3A (UnForceGfxFreq)
- 0x3B (ForceGfxVid) — **DANGER!**
- 0x3C (UnforceGfxVid)
- 0x3D (GetEnabledSmuFeatures) — get enabled feature mask

**DO NOT use ForceGfxFreq/ForceGfxVid/SetCoreEnableMask without DPM initialization!**

---

## Phase 4: Full Linux Amdgpu Init Replication

Complete list of all remaining differences:

| # | Linux Feature | Our Status | Impact |
|---|--------------|------------|--------|
| 1 | SMU DPM Init (PPTable) | ❌ Missing | GFX stays at 15 MHz |
| 2 | MQD in system memory | ❌ Missing | MEC can't configure queue |
| 3 | CP_HQD_PQ_BASE (>>8) | ❌ Using KIQ_BASE_LO | Wrong register |
| 4 | CP_HQD_PERSISTENT_STATE | ❌ Missing | MQD not preloaded |
| 5 | PM4 SET_RESOURCES | ❌ Missing | MEC not told about queues |
| 6 | PM4 MAP_QUEUES (ENGINE_SEL=1) | ❌ Missing | Queue never activated |
| 7 | EOP buffer allocation | ❌ Missing | Required for ring operation |
| 8 | VBIOS ACPI VFCT fetch | ❌ Not attempted | Golden registers may be missing |
| 9 | RLC autoload via PSP | ⚠️ Done via mailbox | Works but no scheduling table |
| 10 | GFX firmware via PSP | ⚠️ Done via mailbox | All types load OK |
| 11 | SMU alive check | ✅ Works | v88.6.0 responding |

---

## Test Plan After Each Phase

1. **After Phase 1**: Run bar5-smn-test → verify GFX freq increases from 15 MHz
2. **After Phase 2**: Run psp-gpu-pm4-submit-test → SCRATCH should change (MEC processes PM4)
3. **After Phase 3**: Verify QueryActiveWgp reports > 0 WGPs
4. **After Phase 4**: Compute dispatch should work (correct-compute-test)

---

## Risk Mitigation

- **SMU ForceGfxFreq crash (Phase 1)**: Do NOT use Force messages before DPM init. Safe: Query, SetSoftMin/Max, GetVersion
- **PM4 ring crash (Phase 2)**: Validate all PM4 packets before submission. Use SEH protection around MMIO.
- **Driver unload crash**: New MQD/buffer allocations must be freed in DreamV3WdmUnload/DxgkDdiRemoveDevice
- **VRAM corruption**: Use system memory (GTT) for MQD + ring, not VRAM (no VRAM init done)

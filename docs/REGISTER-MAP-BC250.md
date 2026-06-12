# BC-250 (Cyan Skillfish) Register Map — GPU BAR5 MMIO

**BIOS:** BC250_5.00_clv.bin  
**BAR5 Physical:** 0xFE800000 (512KB)  
**Date:** 2026-06-12  
**Status:** COMPREHENSIVE — 100+ registers tested

---

## Access Key
- **R** = Read OK
- **W** = Write OK (write-back verified)
- **Wr** = Writes accepted but bits constrained
- **RW** = Read/Write OK
- **RO** = Read-Only (hardware)
- **NBIO** = NBIO blocks writes (native 0xC000+)
- **?** = Not yet tested

## GPU Core Registers

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x0000 | GPU_ID | 0x9FFF9700 | R | Chip ID |
| 0x0004 | GPU_ID2 | 0x00000000 | R | |
| 0x0008 | CHIP_FAMILY | 0x00000000 | R | |
| 0x000C | ASIC_REVISION | 0x00000000 | R | |

## NBIO/Gasket Registers

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0xC060 | CP_ME_CNTL | 0x00000000 | R / NBIO | GC not in space |
| 0xC064 | CP_ME_STATUS | 0x00000000 | R | |
| 0xC0A0 | PFP_UCODE_ADDR | 0x00000000 | R / NBIO | |
| 0xC0A4 | PFP_UCODE_DATA | 0x00000000 | R / NBIO | |
| 0xC0E0 | CP_MEC_CNTL | 0x00000000 | R / NBIO | |
| 0xC0E4 | CP_MEC_STATUS | 0x00000000 | R | |
| 0xC100 | NBIO_ID | 0xFEDCBAEF | R | NBIO hardware ID |

## GC Registers (GC_BASE=0x1260 applied)

### Status & Config (Navi10 0x2000+ → BC-250 0x3260+)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x3260 | GRBM_STATUS | 0x00000000 | R | GC idle |
| 0x3264 | CC_GC_SHADER_ARRAY_CONFIG | 0x00000000 | R | Fused (0 CUs enabled) |
| 0x326C | GRBM_SOFT_RESET | 0x00000000 | RO | Cannot reset GC |
| 0x32D4 | CP_SCRATCH_REG0 | 0x4D585042 | RW | "MDPX" = CP alive |
| 0x34FC | SPI_PG_ENABLE_STATIC_WGP_MASK | 0x00002000 | RW | WGP5 only (bits 8-13 = WGPs) |
| 0x3D64 | RLC_PG_ALWAYS_ON_WGP_MASK | 0x00000000 | R | |

### CP Ring Registers (Navi10 0xC800+ → BC-250 0xDA60+)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0xDA60 | CP_RING0_BASE_LO | 0x00000000 | RO | **READ-ONLY — blocker** |
| 0xDA64 | CP_RING0_BASE_HI | 0x00000000 | RO | |
| 0xDA68 | CP_RING0_CNTL | 0x00000000 | Wr | Bits constrained (0→0x1) |
| 0xDA6C | CP_RING0_RPTR | 0x01200000 | Wr | Writes accepted, bits modified |
| 0xDA70 | CP_RING0_RPTR_ADDR_LO | ? | ? | Not tested |
| 0xDA74 | CP_RING0_RPTR_ADDR_HI | ? | ? | Not tested |
| 0xDA78 | CP_RING0_WPTR | 0x00100010 | Wr | Writes accepted, bits modified |
| 0xDA7C | CP_RING0_WPTR_POLL | ? | ? | Not tested |
| 0xDA80 | CP_RING0_DOORBELL | ? | ? | Not tested |

### Compute Ring Registers (Navi10 0xC900+ → BC-250 0xDB60+)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0xDB60 | COMPUTE_RING0_BASE_LO | 0x00000000 | RO | |
| 0xDB64 | COMPUTE_RING0_BASE_HI | 0x00000000 | W | |
| 0xDB68 | COMPUTE_RING0_CNTL | 0x00000000 | W | |
| 0xDB6C | COMPUTE_RING0_RPTR | 0x00000000 | W | |
| 0xDB78 | COMPUTE_RING0_WPTR | 0x00000000 | W | |

### KIQ Registers (Navi10 0xCE00+ → BC-250 0xE060+)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0xE060 | KIQ_BASE_LO | 0x00000000 | **W** | **ONLY writable BASE** |
| 0xE064 | KIQ_BASE_HI | 0x00000000 | ? | Not tested |
| 0xE068 | KIQ_CNTL | 0x00000000 | RO | Cannot enable ring |
| 0xE06C | KIQ_RPTR | 0x00000000 | W | |
| 0xE078 | KIQ_WPTR | 0x00000000 | W | |

## Interrupt Handler

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x3800 | IH_RB_BASE_LO | ? | ? | |
| 0x3808 | IH_RB_CNTL | ? | ? | |
| 0x3810 | IH_RB_RPTR | ? | ? | |
| 0x3820 | IH_CNTL | ? | ? | |

## Memory Controller (MC)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x0520 | MC_VM_FB_LOCATION_BASE | 0x00000000 | ? | |
| 0x0524 | MC_VM_FB_LOCATION_TOP | 0x00000000 | ? | |
| 0x0528 | MC_VM_AGP_BASE | 0x00000000 | ? | |

## HDP (Host Data Path)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x12A0 | HDP_MEM_COHERENCY_FLUSH_CNTL | ? | ? | |
| 0x12B0 | HDP_DEBUG0 | ? | ? | |
| 0x12C0 | HDP_NONSURFACE_INFO | ? | ? | |

## GB (Graphics Backend)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x9800 | GB_ADDR_CONFIG | ? | ? | Not verified |
| 0x9804 | GB_ADDR_CONFIG_READ | ? | ? | |

## MMHUB

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x5000+ | Various | varies | RW | Memory management |

## VM (GPUVM)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x1A00 | VM_CONTEXT0_CNTL | ? | ? | |
| 0x1A04 | VM_CONTEXT0_PAGE_TABLE_BASE_ADDR | ? | ? | |
| 0x1A80 | VM_INVALIDATE_ENG0_REQ | ? | ? | |
| 0x1A84 | VM_INVALIDATE_ENG0_ACK | ? | ? | |

## Display (DCN)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x5080 | DCSURF_PRIMARY_SURFACE_ADDRESS | ? | ? | |
| 0x6000 | OTG0_CONTROL | ? | ? | |
| 0x6010 | OTG0_CRTC_V_TOTAL | ? | ? | |
| 0x6014 | OTG0_CRTC_H_TOTAL | ? | ? | |

## Thermal (THM)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x8000 | THM_THERMAL_CTRL | 0x0018 | RW | THM_BASE=0x8000 (correct) |
| 0x8008 | THM_CURRENT_TEMP | varies | R | Temperature sensor |

## Data Fabric (DF)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x1A000+ | Various | varies | R | Read-only |

## SDMA

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0xE000 | SDMA0_GFX_RB_BASE_LO | 0x00000000 | ? | Navi10 offset? |
| 0xE008 | SDMA0_GFX_RB_CNTL | 0x00000000 | ? | |
| 0xE010 | SDMA0_GFX_RB_WPTR | 0x00000000 | R | |

## SMU (MP1_BASE=0x16000)

| Offset | Register | Value (BIOS 5.00) | Access | Notes |
|--------|----------|-------------------|--------|-------|
| 0x16A08 | C2PMSG_66 | 0x00000000 | RW | Message register (no response) |
| 0x16A48 | C2PMSG_82 | 0x00000000 | RW | Argument (no response) |
| 0x16A4C | C2PMSG_83 | 0x00000000 | RW | Extended data |
| 0x16A68 | C2PMSG_90 | 0x00000000 | RW | Response status (always 0) |

SMU firmware does NOT respond to any messages. Likely power-gated or firmware not loaded.

## Summary by Block

| Block | Base | Native Range | Shifted | Notes |
|-------|------|-------------|---------|-------|
| GC config | 0x1260 | 0x0000-0x2FFF | 0x1260-0x3FFF | GRBM, CC, SPI, scratch, RLC |
| GC rings | 0x1260 | 0xC800-0xCE20 | 0xDA60-0xE080 | Ring, compute, KIQ, HQD |
| CP control | N/A | 0xC060-0xC0FF | NOT shifted | NBIO space, writes blocked |
| IH | 0x3800 | 0x3800-0x3820 | Not shifted | Interrupt handler |
| MC/VM | 0x500 | 0x0500-0x0550 | Not shifted | Memory controller |
| HDP | 0x12A0 | 0x12A0-0x12D0 | Not shifted | Host data path |
| VM | 0x1A00 | 0x1A00-0x1A90 | Not shifted | GPUVM |
| MMHUB | 0x5000 | 0x5000-0x5300 | Not shifted | Memory hub |
| DCN | 0x5000 | 0x5000-0x6028 | Not shifted | Display |
| DMCUB | 0x7000 | 0x7000-0x701C | Not shifted | Display MCU |
| THM | **0x8000** | 0x8000-0x8008 | Not shifted | Thermal (NOT 0x16600) |
| GB | 0x9800 | 0x9800-0x9804 | Not shifted | Graphics backend |
| SDMA | 0xE000? | 0xE000-0xE018 | Unknown | SDMA engine |
| SMU | 0x16000 | 0x16000-0x16A68 | Not shifted | Power management |
| DF | 0x1A000 | 0x1A000-0x1A500 | Not shifted | Data fabric |

## References

- `GC_BASE-REGISTER-ANALYSIS.md` — detailed register-by-register analysis
- `NBIO-FIREWALL-ANALYSIS.md` — which registers NBIO blocks
- `RING-INIT-STATUS.md` — ring init blockers and KIQ path
- `../../test-tools/cp-probe-test.cs` — write-back test source code
- `../../test-tools/cp-ring2-test.cs` — ring register probe source code
- `../../inc/amdbc250_dream_hw.h` — register definitions in driver

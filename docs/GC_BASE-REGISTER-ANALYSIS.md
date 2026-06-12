# GC_BASE Register Offset Analysis — BC-250 (Cyan Skillfish)

**BIOS:** BC250_5.00_clv.bin  
**Date:** 2026-06-12  
**Status:** CONFIRMED — all GC registers shifted by GC_BASE=0x1260

---

## The GC_BASE Problem

BC-250 (Cyan Skillfish) uses a non-standard BAR5 register layout. Unlike standard Navi10 where GC registers start at BAR5+0x0000, BC-250 shifts all GC registers by `GC_BASE__INST0_SEG0 = 0x1260`.

From `linux/drivers/gpu/drm/amd/include/cyan_skillfish_ip_offset.h`:
```c
GC_BASE__INST0_SEG0 = 0x00001260  // Segment 0: most GC registers
GC_BASE__INST0_SEG1 = 0x0000A000  // Segment 1: other GC registers
```

The formula:
```
BAR5_offset = GC_BASE + Navi10_mmRegister_offset
           = 0x1260 + Navi10_offset
```

## Register Offset Correction Table

### GC Configuration Registers (0x2000-0x2FFF native)

| Register | Navi10 Offset | BC-250 Offset | Value (BIOS 5.00) | Access |
|----------|--------------|---------------|-------------------|--------|
| GRBM_STATUS | 0x2000 | **0x3260** | 0x00000000 | R |
| CC_GC_SHADER_ARRAY_CONFIG | 0x2004 | **0x3264** | 0x00000000 (fused) | R |
| GRBM_SOFT_RESET | 0x200C | **0x326C** | 0x00000000 | R (write ignored) |
| CP scratch | 0x2074 | **0x32D4** | 0x4D585042 ("MDPX") | R/W |
| SPI_PG_ENABLE_STATIC_WGP_MASK | 0x229C | **0x34FC** | 0x00002000 | R/W |
| RLC_PG_ALWAYS_ON_WGP_MASK | 0x2B04 | **0x3D64** | 0x00000000 | R |

### CP Ring Registers (0xC800-0xC820 native → 0xDA60-0xDA80 shifted)

**CRITICAL: NBIO blocks writes to native 0xC000+ range, but GC_BASE-shifted aliases (0xDA60+) bypass NBIO.**

| Register | Native (NBIO) | GC_BASE-shifted | Access (shifted) |
|----------|--------------|-----------------|-------------------|
| CP_RING0_BASE_LO | 0xC800 (R only) | **0xDA60** | **READ-ONLY** |
| CP_RING0_BASE_HI | 0xC804 (R only) | **0xDA64** | **READ-ONLY** |
| CP_RING0_CNTL | 0xC808 (R only) | **0xDA68** | **WRITABLE** (0→0x1) |
| CP_RING0_RPTR | 0xC80C (R only) | **0xDA6C** | **WRITABLE** (0→0xDEADBEEF) |
| CP_RING0_RPTR_ADDR_LO | 0xC810 | **0xDA70** | untested |
| CP_RING0_RPTR_ADDR_HI | 0xC814 | **0xDA74** | untested |
| CP_RING0_WPTR | 0xC818 (R only) | **0xDA78** | **WRITABLE** (0→0xDEADBEEF) |
| CP_RING0_WPTR_POLL | 0xC81C | **0xDA7C** | untested |
| CP_RING0_DOORBELL | 0xC820 | **0xDA80** | untested |

### Compute Ring Registers (0xC900+ native → 0xDB60+ shifted)

| Register | Native | GC_BASE-shifted | Access (shifted) |
|----------|--------|-----------------|-------------------|
| COMPUTE_RING0_BASE_LO | 0xC900 | **0xDB60** | **READ-ONLY** |
| COMPUTE_RING0_BASE_HI | 0xC904 | **0xDB64** | **WRITABLE** (0→0xDEADBEEF) |
| COMPUTE_RING0_CNTL | 0xC908 | **0xDB68** | **WRITABLE** |
| COMPUTE_RING0_RPTR | 0xC90C | **0xDB6C** | **WRITABLE** |
| COMPUTE_RING0_WPTR | 0xC918 | **0xDB78** | **WRITABLE** |

### KIQ Registers (0xCE00+ native → 0xE060+ shifted)

| Register | Native | GC_BASE-shifted | Access (shifted) |
|----------|--------|-----------------|-------------------|
| KIQ_BASE_LO | 0xCE00 | **0xE060** | **WRITABLE** ← ONLY writable BASE |
| KIQ_BASE_HI | 0xCE04 | **0xE064** | untested |
| KIQ_CNTL | 0xCE08 | **0xE068** | **READ-ONLY** (0 stays 0) |
| KIQ_RPTR | 0xCE0C | **0xE06C** | **WRITABLE** |
| KIQ_WPTR | 0xCE18 | **0xE078** | **WRITABLE** |

### HQD Registers (0xC860+ native → 0xDAC0+ shifted)

| Register | Native | GC_BASE-shifted | Access (shifted) |
|----------|--------|-----------------|-------------------|
| HQD_ACTIVE | 0xC860 | **0xDAC0** | not yet tested |
| HQD_VMID | 0xC864 | **0xDAC4** | not yet tested |
| HQD_PERSISTENT_STATE | 0xC868 | **0xDAC8** | not yet tested |
| HQD_SEMA_CMD | 0xC870 | **0xDAD0** | not yet tested |

### CP Control Registers (0xC060-0xC0FF — NOT shifted!)

These are at NBIO address space, NOT in GC space. GC_BASE-shifted aliases (0xD2C0+) return 0xFFFFFFFF (unmapped).

| Register | Offset | Access | Notes |
|----------|--------|--------|-------|
| CP_ME_CNTL | 0xC060 | R (value=0), W ignored | NBIO blocks ALL writes |
| CP_ME_STATUS | 0xC064 | R | NBIO address |
| CP_PFP_UCODE_ADDR | 0xC0A0 | R, W ignored | NBIO blocks |
| CP_PFP_UCODE_DATA | 0xC0A4 | R, W ignored | NBIO blocks |
| CP_ME_UCODE_ADDR | 0xC0B0 | R, W ignored | NBIO blocks |
| CP_ME_UCODE_DATA | 0xC0B4 | R, W ignored | NBIO blocks |
| CP_MEC_CNTL | 0xC0E0 | R (value=0), W ignored | NBIO blocks |
| CP_MEC_STATUS | 0xC0E4 | R | NBIO address |

## Key Insight: NBIO vs Hardware Read-Only

There are TWO separate mechanisms preventing writes:

1. **NBIO firewall**: Blocks ALL writes to 0xC000-0xCFFF range from ALL paths (GPU BAR5, PSP BAR0, SMN). Reading returns 0. GC_BASE-shifted aliases (0xDA60+) bypass this.

2. **Hardware read-only**: Even at GC_BASE-shifted aliases, some registers like BASE_LO are read-only by hardware design. These accept the NBIO bus cycle but the register logic ignores writes. This is common for registers that are fused or latched at power-on.

## Write Test Results (Windows GPU BAR5 direct MMIO)

From `cp-probe-test.exe` write-back tests:
```
CP_RING0_BASE_LO  [0xDA60] 0x0 → 0xDEADBEEF → read 0x0        = READ-ONLY
CP_RING0_BASE_HI  [0xDA64] 0x0 → 0xDEADBEEF → read 0x0        = READ-ONLY
CP_RING0_CNTL     [0xDA68] 0x0 → 0xDEADBEEF → read 0x1        = WRITABLE (bits constrained)
CP_RING0_RPTR     [0xDA6C] 0x0 → 0xDEADBEEF → read 0x00A1B2EF = WRITABLE (bits modified)
CP_RING0_WPTR     [0xDA78] 0x0 → 0xDEADBEEF → read 0x0EAD0EEF = WRITABLE (bits modified)
COMPUTE_BASE_LO   [0xDB60] 0x0 → 0xDEADBEEF → read 0x0        = READ-ONLY
COMPUTE_BASE_HI   [0xDB64] 0x0 → 0xDEADBEEF → read 0xDEADBEEF = WRITABLE
COMPUTE_CNTL      [0xDB68] 0x0 → 0xDEADBEEF → read varies     = WRITABLE
COMPUTE_RPTR      [0xDB6C] 0x0 → 0xDEADBEEF → read varies     = WRITABLE
COMPUTE_WPTR      [0xDB78] 0x0 → 0xDEADBEEF → read varies     = WRITABLE
KIQ_BASE_LO       [0xE060] 0x0 → 0xDEADBEEF → read 0xDEADBEEF = WRITABLE ← THE ONE!
KIQ_CNTL          [0xE068] 0x0 → 0xDEADBEEF → read 0x0        = READ-ONLY
KIQ_RPTR          [0xE06C] 0x0 → 0xDEADBEEF → read 0xEF       = WRITABLE
KIQ_WPTR          [0xE078] 0x0 → 0xDEADBEEF → read 0xEF       = WRITABLE
SPI_WGP_MASK      [0x34FC] 0x2000 → 0x3F00 → read 0x3F00     = WRITABLE ← restored OK
```

## Why KIQ is the Primary Path Forward

- KIQ_BASE_LO at 0xE060 is the **ONLY writable ring BASE register** on BC-250
- All GFX ring (0xDA60) and compute ring (0xDB60) BASE_LO registers are read-only
- KIQ_WPTR at 0xE078 is writable — can trigger command execution
- KIQ_CNTL at 0xE068 is read-only — WARNING: might prevent ring enable

## Driver Changes Applied

1. **hw.h**: All CP ring (0xC800+), compute (0xC900+), HQD (0xC860+), KIQ (0xCE00+) shifted by GC_BASE
2. **hw.h**: Scratch registers fixed from 0x8500 to GC_BASE+0x2074 (0x32D4)
3. **hw.h**: KIQ registers added
4. **kmd.h**: GC_BASE/MP1_BASE/THM_BASE moved before hw.h include
5. **hw_init.c**: CP_ME_CNTL writes at 0xC060 — NBIO blocked, non-fatal
6. **PSP driver (separate repo)**: 7 hardcoded Navi10 offsets fixed with AMDBC250_GC_BASE

## References

- `linux/drivers/gpu/drm/amd/include/cyan_skillfish_ip_offset.h`
- `linux/drivers/gpu/drm/amd/include/gc/v10/gc_10_1_0_offset.h`
- `test-tools/cp-probe-test.cs` — write-back test source
- `test-tools/cp-ring2-test.cs` — ring register probe
- `inc/amdbc250_dream_hw.h` — corrected register defines
- `inc/amdbc250_dream_kmd.h` — GC_BASE constant

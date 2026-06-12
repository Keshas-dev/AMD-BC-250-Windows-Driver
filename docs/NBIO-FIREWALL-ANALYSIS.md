# NBIO Firewall Analysis — BC-250 (Cyan Skillfish)

**BIOS:** BC250_5.00_clv.bin  
**Date:** 2026-06-12  
**Status:** COMPLETE — Two separate blocking mechanisms identified

---

## Summary

NBIO on BC-250 has TWO distinct access-control behaviors:

1. **0xC000-0xCFFF range (native NBIO addresses)**: ALL writes blocked from ALL paths
2. **GC registers at 0x2000-0x3FFF range**: NOT blocked — previous 0xFFFFFFFF were wrong offsets

## Phase 1: The "All GC Registers Blocked" Myth (Disproved 2026-06-11)

Early testing used Navi10 register offsets to read GC registers:
- GRBM_STATUS at 0x2004 → 0xFFFFFFFF
- CP scratch at 0x2074 → 0xFFFFFFFF
- CC_GC_SHADER_ARRAY_CONFIG at 0x2004 → 0xFFFFFFFF

All returned 0xFFFFFFFF, which we incorrectly attributed to NBIO firewall (common on PS5/Navi10).

**Actual cause:** BC-250 uses GC_BASE=0x1260 shift. Register at BAR5+0x2004 is unmapped. Correct offset is BAR5+0x3264 (= 0x1260 + 0x2004).

**Linux CachyOS devmem confirmation:**
| Address | Register | Value | Verdict |
|---------|----------|-------|---------|
| 0xFE803264 | CC_GC_SHADER_ARRAY_CONFIG | 0x0 | NBIO NOT blocking |
| 0xFE8034FC | SPI_PG_ENABLE_STATIC_WGP_MASK | 0x2000 | NBIO NOT blocking |
| 0xFE803260 | GRBM_STATUS | 0x0 | NBIO NOT blocking |
| 0xFE8032D4 | CP scratch | 0x4D585042 | CP alive |

## Phase 2: NBIO Actually Blocks 0xC000-0xCFFF (Confirmed 2026-06-12)

The NBIO firewall on BC-250 specifically blocks the 0xC000-0xCFFF range. This affects:
- CP_ME_CNTL/MEC_CNTL (0xC060/0xC0E0) — halt/resume impossible
- PFP/ME firmware loading registers (0xC0A0-0xC0B4)
- ALL CP ring registers at native offsets (0xC800-0xC81C)

**NBIO blocking characteristics:**
- Reading returns 0x00000000 (not 0xFFFFFFFF)
- Writes are silently ignored (no error, no hang)
- Blocks ALL access paths: GPU BAR5, PSP BAR0, SMN, PCI config

### CP_ME_CNTL at 0xC060 — NOT in GC Space

CP_ME_CNTL at NBIO address 0xC060 is NOT accessible at GC_BASE-shifted offset:
- Native: 0xC060 → 0x00000000 (register exists, writes ignored)
- GC_BASE-shifted: 0xD2C0 → 0xFFFFFFFF (unmapped)

This means CP_ME_CNTL is in NBIO space, not GC space. NBIO blocks all writes.

## Phase 3: GC_BASE-Shifted Aliases Bypass NBIO (Confirmed 2026-06-12)

The GC_BASE aliases (0xDA60 = 0x1260 + 0xC800) bypass the NBIO firewall. These addresses map to the same hardware registers but through a different path that NBIO doesn't intercept.

**Why aliases work:**
- BAR5 decoding: The PCIe BAR5 decoder maps addresses to IP blocks
- GC_BASE aliases are decoded as GC space, not NBIO space
- NBIO only intercepts the native 0xC000+ range

**Write-back test results at GC_BASE-shifted aliases:**
| Register | Shifted Addr | Native Addr | Writable? |
|----------|-------------|-------------|-----------|
| CP_RING0_CNTL | 0xDA68 | 0xC808 | YES — 0x0→0x1 |
| CP_RING0_RPTR | 0xDA6C | 0xC80C | YES — accepts non-zero |
| CP_RING0_WPTR | 0xDA78 | 0xC818 | YES — accepts non-zero |
| CP_RING0_BASE_LO | 0xDA60 | 0xC800 | NO — hardware read-only |
| KIQ_BASE_LO | 0xE060 | 0xCE00 | YES — 0x0→0xDEADBEEF |
| KIQ_CNTL | 0xE068 | 0xCE08 | NO — hardware read-only |

## Why Writes to Alias Addresses Still Fail for BASE_LO

Even though NBIO doesn't block the aliases, some registers are **hardware read-only**:
- CP_RING0_BASE_LO/HI: These contain fused/latched values or are protected by a locked state
- KIQ_CNTL: Control register with enable bits set by firmware

These are NOT NBIO-blocked — the register logic itself rejects writes.

## SMU (MP1_BASE) — No NBIO Issue, SMU Firmware Not Responding

SMU at MP1_BASE=0x16000 is NOT affected by NBIO firewall. Both Navi10 (0x16104+) and BC-250 (0x16A08+) C2PMSG offsets return readable zeros. SMU firmware likely not running (power-gated or missing).

## THM_BASE — Corrected from Linux Headers

- Linux suggests: THM_BASE = 0x16600
- **Hardware confirmed**: THM_BASE = 0x8000
- 0x8000 returns 0x18 (writable), 0x8008 returns temperature
- 0x1662C returns 0x00000000 (wrong address)

This is unrelated to NBIO — the Linux IP offset headers may have wrong values for our BIOS/silicon revision.

## Complete Block Map (GPU BAR5)

| Range | Block | NBIO Blocks? | Notes |
|-------|-------|-------------|-------|
| 0x0000-0x000F | GPU_ID/NBIO | No | Read-only chip ID |
| 0x0500-0x0540 | MC VM | No | Memory controller |
| 0x05A0-0x05C0 | HDP | Reads OK, writes blocked | HDP coherency |
| 0x0E00-0x0E1F | HW_ID | No | Chip identification |
| 0x1260-0x5260 | GC (shifted) | NO | Main GC register block |
| 0x3260-0x32D4 | GC status/scratch | No | GRBM, CC, scratch |
| 0x34FC | SPI | No | WGP mask — WRITABLE! |
| 0x3800-0x3820 | IH | No | Interrupt handler |
| 0x3D64 | RLC PG | No | Power gating mask |
| 0x5000-0x5300 | MMHUB | No | Memory hub |
| 0x6000-0x6028 | OTG/DCN | No | Display timing |
| 0x7000-0x701C | DMCUB | No | Display MCU |
| 0x8000-0x8008 | THM | No | Thermal sensor |
| 0x9800-0x9804 | GB_ADDR | No | GB config |
| 0xC000-0xC0FF | CP control | **YES** | ME_CNTL, PFP/ME ucode |
| 0xC800-0xC81C | CP ring (native) | **YES** | All ring registers |
| 0xC860-0xC870 | HQD (native) | **YES** | HW queue dispatcher |
| 0xC900-0xC918 | Compute ring (native) | **YES** | Compute ring |
| 0xCE00-0xCE18 | KIQ (native) | **YES** | Kernel interface queue |
| 0xDA60-0xDA80 | CP ring (GC alias) | **NO (alias)** | BASE_LO read-only |
| 0xDB60-0xDB78 | Compute ring (alias) | **NO (alias)** | BASE_LO read-only |
| 0xDAC0-0xDAD0 | HQD (alias) | **NO (alias)** | Not yet tested |
| 0xE060-0xE078 | KIQ (alias) | **NO (alias)** | BASE_LO WRITABLE! |
| 0xE000-0xE018 | SDMA0 | Unknown | SDMA separate block |
| 0x16000-0x16A68 | MP1/SMU | No | SMU not responding |
| 0x1A000-0x1A500 | DF | No | Data fabric |

## Timeline

| Date | Event |
|------|-------|
| 2026-06-03 | First NBIO register scan at Navi10 offsets — 6 readable, 7 "blocked" |
| 2026-06-10 | GC_BASE=0x1260 discovered in Linux cyan_skillfish_ip_offset.h |
| 2026-06-11 | Linux devmem confirms all GC registers readable at corrected offsets |
| 2026-06-11 | Windows confirms same — previous 0xFFFFFFFF were wrong offsets |
| 2026-06-12 | SPI_PG_ENABLE_STATIC_WGP_MASK confirmed WRITABLE at 0x34FC |
| 2026-06-12 | NBIO confirmed to block 0xC000+ from ALL paths |
| 2026-06-12 | GC_BASE-shifted aliases (0xDA60+) bypass NBIO bridge |
| 2026-06-12 | CP_RING0_BASE_LO at 0xDA60 confirmed hardware read-only |
| 2026-06-12 | KIQ_BASE_LO at 0xE060 confirmed WRITABLE — only writable BASE |

## References

- `GC_BASE-REGISTER-ANALYSIS.md` — register offset details
- `RING-INIT-STATUS.md` — ring initialization status
- `test-tools/cp-probe-test.cs` — write-back test source

# NBIO Firewall Analysis — BC-250 (Cyan Skillfish)

**Status:** RESOLVED — NBIO does NOT block GC registers at corrected BC-250 offsets

---

## The Problem That Wasn't

Early testing used **Navi10 register offsets** (BAR5+0x0000) to read GC registers like GRBM_STATUS (0x2004), CP scratch (0x2074), and SDMA (0x2600). All returned **0xFFFFFFFF**, leading us to believe the NBIO firewall was blocking GC register access on the PS5-derived BC-250.

## Actual Cause

BC-250 (Cyan Skillfish) uses a **non-standard BAR5 register layout** vs Navi10. GC registers are shifted by `GC_BASE__INST0_SEG0 = 0x1260`:

```
BAR5_offset = 0x1260 + Navi10_register_offset
```

All reads at the old Navi10 offsets (0x2000-0x2FFF) hit unmapped address space — hence 0xFFFFFFFF.

## Confirmed: NBIO Does NOT Block

### Linux CachyOS devmem (cold boot)
| Address | Expected | Result |
|---------|----------|--------|
| `0xFE803264` (CC_GC_SHADER_ARRAY_CONFIG) | GC register | **0x0** — readable |
| `0xFE8034FC` (SPI_PG_ENABLE_STATIC_WGP_MASK) | GC register | **0x2000** — readable |
| `0xFE803260` (GRBM_STATUS) | GC register | **0x0** — readable |
| `0xFE8032D4` (CP scratch) | GC register | **0x4D585042** — readable |
| `0xFE803860` (SDMA) | GC register | **0x8E0** — readable |

No freezing, no 0xFFFFFFFF, no hangs.

### Windows driver (same offsets)
Identical values confirmed via `safe-test.exe` and `deep-test.exe`.

## What's Actually Inaccessible

| Block | Offset | Reason |
|-------|--------|--------|
| CLK | 0x0D00-0x0DFF | Likely always blocked on PS5 derivatives |
| UVD | 0x2300+ | Locked by Sony firmware |
| RSMU | 0xA000+ | System Management Unit |
| PSP C2PMSG | 0x3E880+ | PSP-private, need PSP driver |
| HDP writes | 0x05A0+ | Reads work, writes silently ignored |

## Why We Still Can't Write to CC/SPI

Despite NBIO allowing reads, writes to `CC_GC_SHADER_ARRAY_CONFIG` (0x3264) and `SPI_PG_ENABLE_STATIC_WGP_MASK` (0x34FC) are silently ignored. This is **not** an NBIO issue — it's because:
1. GC block is likely **power-gated** (needs SMU `PowerUpGfx` message ID 0x6)
2. Harvest registers may be read-only fuses, not writable
3. Linux reports 24 CUs via Virtual CRAT table, not register writes

## Timeline

| Date | Event |
|------|-------|
| 2026-06-03 | First NBIO register scan at Navi10 offsets — 6 readable, 7 blocked |
| 2026-06-10 | GC_BASE=0x1260 offset difference discovered in Linux source |
| 2026-06-11 | Linux devmem confirms **all** GC registers readable at corrected offsets |
| 2026-06-11 | Windows confirms same — NBIO not blocking, all previous 0xFFFFFFFF were wrong offsets |

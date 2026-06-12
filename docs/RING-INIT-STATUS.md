# Ring Initialization Status — BC-250 (Cyan Skillfish)

**BIOS:** BC250_5.00_clv.bin  
**Date:** 2026-06-12  
**Status:** BLOCKED — GFX ring BASE_LO read-only, KIQ path identified

---

## Current Ring Init Code

The driver's `DreamV3HwInitGfxRing` function (in `src/kmd/amdbc250_dream_hw_init.c`) does:

```
Step 1: Halt CP via CP_ME_CNTL (0xC060)
  → NBIO blocks write → silently ignored
Step 2: Write ring buffer PA to BASE_LO/HI (now at 0xDA60/0xDA64)
  → BASE_LO is READ-ONLY → write silently ignored
Step 3: Write ring control to CNTL (0xDA68)
  → WRITABLE → works
Step 4: Clear RPTR/WPTR (0xDA6C/0xDA78)
  → WRITABLE → works
Step 5: CP scratch test (now at 0x32D4)
  → Should PASS (CP alive, returns 0x4D585042)
Step 6: Resume CP via CP_ME_CNTL (0xC060)
  → NBIO blocks write → silently ignored
```

**Result**: The scratch test passes (CP is alive) so the function returns SUCCESS. But the ring buffer address was never actually programmed because BASE_LO is read-only.

## Blockers

### Blocker 1: GFX Ring BASE_LO Read-Only

`CP_RING0_BASE_LO [0xDA60]`:
- Initial value: 0x00000000
- Wrote 0xDEADBEEF → read back 0x00000000
- **Conclusion: Hardware read-only, cannot set ring buffer address**
- Not an NBIO issue (0xDA60 is a GC_BASE alias, bypasses NBIO)

### Blocker 2: CP Cannot Be Halted/Resumed

`CP_ME_CNTL [0xC060]`:
- NBIO blocks ALL writes to 0xC000+ range
- CP_ME_CNTL is in NBIO space, NOT in GC space
- GC_BASE-shifted address 0xD2C0 returns 0xFFFFFFFF (unmapped)
- **Conclusion: Cannot halt/resume CP through register writes**

### Blocker 3: KIQ CNTL Read-Only

`KIQ_CNTL [0xE068]`:
- Even though BASE_LO is writable, CNTL is read-only
- Without setting CNTL (ring enable bit), ring may not be active
- **Conclusion: Need to determine if KIQ works without CNTL write**

## Working Registers

These registers ARE functional at GC_BASE-shifted offsets:

| Register | Offset | Writable? | Used For |
|----------|--------|-----------|----------|
| CP_RING0_CNTL | 0xDA68 | YES | Ring size, enable |
| CP_RING0_RPTR | 0xDA6C | YES | Read pointer tracking |
| CP_RING0_WPTR | 0xDA78 | YES | Write pointer (PM4 trigger) |
| KIQ_BASE_LO | 0xE060 | YES | Ring buffer address |
| KIQ_RPTR | 0xE06C | YES | Read pointer |
| KIQ_WPTR | 0xE078 | YES | Write pointer (PM4 trigger) |

## Primary Path Forward: KIQ Ring

KIQ (Kernel Interface Queue) is a special CP queue that bypasses the GFX ring. It's used for privileged commands from the kernel driver.

```
┌─────────────────────────────────────────────┐
│ Allocate ring buffer in system memory        │
├─────────────────────────────────────────────┤
│ Write PA to KIQ_BASE_LO [0xE060] ✅ writable │
├─────────────────────────────────────────────┤
│ Write ring size to KIQ_CNTL [0xE068] ❌ RO  │
├─────────────────────────────────────────────┤
│ Write PM4 packets to ring buffer             │
├─────────────────────────────────────────────┤
│ Write updated WPTR to KIQ_WPTR [0xE078] ✅  │
├─────────────────────────────────────────────┤
│ CP processes packets → results in fence/reg │
└─────────────────────────────────────────────┘
```

**Question: Does KIQ work without setting CNTL?**

Possibilities:
1. **KIQ is auto-enabled when BASE is set** — CNTL might just be status
2. **KIQ uses default settings** — 4KB ring, 64-bit addressing
3. **KIQ enable is elsewhere** — a separate register or doorbell

This needs to be tested empirically: set KIQ_BASE_LO, write a NOP PM4, set WPTR, check if WPTR advances (CP consumed the packet).

## Alternative: Use Existing Firmware-Configured Ring

The CP is alive (scratch = 0x4D585042) — firmware loaded PFP/ME/MEC. Maybe there's a default ring configured at a fixed address:

Hypothesis: The PSP/SOS firmware sets up a ring buffer at a known physical address during boot. If we can find this address, we can reuse the existing ring.

Check:
- KIQ_BASE_LO = 0 (not configured)
- CP_RING0_BASE_LO = 0 (not configured)
- No obvious pre-configured ring found

## Scary Path: GRBM Soft Reset

GRBM_SOFT_RESET at 0x326C was also read-only. We can't reset the GC block to potentially make BASE_LO writable.

## Implementation Plan for KIQ

```
1. Allocate ring buffer (contiguous, 4KB-aligned, 4KB-256KB)
2. Add KIQ ring init function:
   - Write buffer PA to KIQ_BASE_LO
   - Write KIQ_WPTR = 0
   - Write ring size to KIQ_CNTL (try, may fail)
3. Add PM4 submission function:
   - Build PM4 command (NOP, WRITE_DATA, DISPATCH_DIRECT)
   - Copy to ring buffer
   - Write WPTR to KIQ_WPTR
   - Poll for completion (fence or RPTR advance)
4. Test:
   - NOP submission → WPTR advances?
   - WRITE_DATA to scratch → value changes?
   - DISPATCH_DIRECT → compute shader runs?
```

## References

- `GC_BASE-REGISTER-ANALYSIS.md` — register offset details
- `spi-write-test.exe` — SPI register write test (works)
- `cp-probe-test.cs` — ring register write-back probe source
- `inc/amdbc250_dream_hw.h` — KIQ register defines added
- `src/kmd/amdbc250_dream_hw_init.c` — ring init implementation

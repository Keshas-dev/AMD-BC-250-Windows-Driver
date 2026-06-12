# PSP Proxy Bypass — GPU Driver Architecture Change

**BIOS:** BC250_5.00_clv.bin  
**Date:** 2026-06-12  
**Commit:** 7eec13a (GPU driver)  
**Status:** IMPLEMENTED — direct GPU BAR5 MMIO always used

---

## The Problem

The GPU driver `atikmdag.sys` has two code paths for register access:

1. **Direct GPU BAR5 MMIO** (`DreamV3ReadRegister`/`DreamV3WriteRegister`): Maps `\\.\AMDBC250DreamV43` IOCTL handler's physical BAR5 (0xFE800000) and reads/writes directly.

2. **PSP proxy** (`Amdbc250PspProxyReadReg`/`Amdbc250PspProxyWriteReg`): Opens `\\.\AmdBcPsp` (PspDriver.sys) and sends IOCTL_READ_REG/IOCTL_WRITE_REG.

**The PSP proxy path was broken** because PspDriver.sys maps a different physical address.

## Root Cause

When PspDriver.sys receives `PSP_READ_REG`/`PSP_WRITE_REG`, it reads/writes through its own BAR5 mapping. On BC-250:

- **GPU BAR5** (GPU function 0): Physical address **0xFE800000** (512KB) — GPU MMIO registers
- **PSP BAR5** (PSP function 2, 1022:143E): Physical address **0xFD600000** — PSP control registers

These are DIFFERENT physical address spaces. When the PSP driver receives a register offset (e.g., 0x3264 for CC_GC_SHADER_ARRAY_CONFIG), it reads:
```
PSP_BAR0 + 0x3264 = 0xFD603264
```
But the GPU register is at:
```
GPU_BAR5 + 0x3264 = 0xFE803264
```

The PSP BAR0 mapping returns 0xFFFFFFFF for non-PSP registers because PSP BAR0 doesn't decode those addresses.

## The Fix

Changed the READ_REG/WRITE_REG IOCTL handlers in `amdbc250_dream_kmd.c` to **always use the direct GPU BAR5 MMIO path**, bypassing the PSP proxy entirely:

```c
// BEFORE (commit e513122):
if (Amdbc250PspProxyAvailable()) {
    Value = Amdbc250PspProxyReadReg(RegisterOffset);
} else {
    Value = DreamV3ReadRegister(DevExt, RegisterOffset);
}

// AFTER (commit 7eec13a):
Value = DreamV3ReadRegister(DevExt, RegisterOffset);  // Always direct MMIO
```

## Why This is Safe

- GPU BAR5 direct MMIO works correctly for ALL accessible registers (tested on 80+ registers)
- NBIO does NOT block GC registers at corrected BC-250 offsets
- The PSP proxy adds unnecessary complexity and a wrong BAR mapping
- Bypassing the proxy doesn't affect other PSP driver functions (SOS loading, NBIO unlock)

## What PSP Proxy Was Supposed to Do

The original architecture assumed:
1. PSP would provide a way to bypass NBIO firewall (wrong assumption — NBIO wasn't blocking)
2. PSP has separate MMIO space that might behave differently (wrong — PSP maps wrong BAR)
3. PSP rings could route register writes around NBIO (tested — ring protocol not supported by SOS)

None of these assumptions proved correct on BC-250.

## What the PSP Driver Still Provides

The PSP driver (`PspDriver.sys`) is still needed for:
- **SOS firmware status** — checking if PSP is alive (C2PMSG_81)
- **NBIO unlock** — IF we ever find a working unlock sequence
- **Future PSP features** — if SOS FW is updated to support ring protocol

## Files Changed

| File | Change |
|------|--------|
| `src/kmd/amdbc250_dream_kmd.c` | READ_REG/WRITE_REG handlers bypass proxy |
| `src/kmd/amdbc250_dream_hw_init.c` | Ring init uses DreamV3Read/WriteRegister directly |
| `src/kmd/amdbc250_psp.c` | Proxy functions orphaned but preserved |

## Test Results

All register reads at corrected GC_BASE offsets return identical values through both paths (GPU BAR5 direct vs PSP BAR0).
The only difference: PSP BAR0 returns 0xFFFFFFFF for non-PSP registers, while GPU BAR5 returns correct values.

## References

- `NBIO-FIREWALL-ANALYSIS.md` — why NBIO isn't blocking
- `GC_BASE-REGISTER-ANALYSIS.md` — corrected register offsets
- `RING-INIT-STATUS.md` — current ring init status

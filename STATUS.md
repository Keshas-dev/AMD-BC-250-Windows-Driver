# AMD BC-250 Windows Driver — Status Report

**Data:** 2026-06-21
**Versija:** v4.4.0
**Projektas:** Windows GPU driver for AMD BC-250 (Cyan Skillfish / RDNA2)

---

## Quick Status

| Komponentas | Statusas |
|-------------|----------|
| KMD loads, IOCTLs work (30+ handlers) | ✅ |
| BAR5 MMIO via DreamV3WriteRegister | ✅ |
| PSP proxy — firmware load, NBIO unlock, KIQ submit | ✅ |
| CP firmware load via MMIO IC_BASE DMA | ✅ |
| Build + sign pipeline (build.bat) | ✅ |
| Vulkan ICD works with official loader | ✅ |
| D3D9 UMD (45+ DDI functions) | ✅ |
| **PM4 execution via GCVM** | ❌ **BLOCKED** |

---

## Critical Blocker — GCVM Page Table Translation

GPU cannot access system RAM via GCVM translation. PM4 commands in ring buffer are invisible to GPU.

### Root Cause
1. **BIOS configures GCVM at boot** — 12 compute queues, ring at PA 0x7E522000
2. **PT_BASE hardware-locked** at OLD offset 0x0B608 (always reads 0)
3. **PT_BASE writable at Linux offset 0x6C8C** (= BIOS PML4 0x7E511000)
4. **Replacing PT_BASE breaks existing mappings** — new page tables only map ring buffer
5. **GPU_KIQ_TEST replaces PT_BASE** → GPU can't access ring

### Register Offset Truth Table (Verified 2026-06-21)

| Register | OLD Offset | Linux Offset | Writable At | Value |
|----------|-----------|-------------|-------------|-------|
| GCVM_CONTEXT0_CNTL | 0x0B460 | 0x6AE0 | **OLD** | 0x010CA88D |
| GCVM_CONTEXT0_PT_BASE | 0x0B608 | 0x6C8C | **Linux** | 0x7E511000 |
| GCVM_L2_CNTL | 0x0B360 | 0x69E0 | **OLD** | 0x413C6798 |
| SCRATCH | 0x32D4 | 0x32D4 | Both | 0x4D585042 |

---

## Win11 26100 MMIO Issue

Direct BAR5 volatile writes silently dropped:
```c
*(volatile PULONG)(mmio + off) = val;  // ❌ SILENTLY DROPPED
DreamV3WriteRegister(DevExt, off, val); // ✅ WORKS (WRITE_REGISTER_ULONG)
```

**Impact:** All register writes in GPU_KIQ_TEST must use `DreamV3WriteRegister`.

---

## BIOS GCVM Configuration (Verified)

| Register | Value | Notes |
|----------|-------|-------|
| SCRATCH (0x32D4) | 0x4D585042 | "MXPB" BIOS marker |
| KIQ_BASE_LO | 0x7E522000 | BIOS KIQ ring physical address |
| KIQ_WPTR | 0x00000008 | BIOS already kicked 8 DWORDs |
| KIQ_RPTR | 0x00000000 | CP didn't process (halted) |
| ME_CNTL | 0xFFFBD9FB | ME+PFP halted |
| HQD_ACTIVE | 0x0000FFF0 | 12 compute queues active |
| PT_BASE (0x6C8C) | 0x7E511000 | BIOS PML4 |
| CTX0_CNTL (0x0B460) | 0x010CA88D | bit0=enable, bit1=DEFAULT_PAGE |

---

## Test Results

| Test | Status | Notes |
|------|--------|-------|
| gpu-kiq-test.exe | ❌ SCRATCH unchanged | Registers programmed, PM4 not executed |
| kiq-diag.exe | ✅ Full diagnostic | Proves DreamV3WriteRegister works |
| kiq-unhalt.exe | ✅ ME_CNTL writable | Clearing halt alone insufficient |
| iommu-gcvm-check.exe | ✅ READ-ONLY scan | IOMMU zeros all GCVM regs |
| test-pt-base-writable.exe | ✅ PT_BASE verified | Linux offset 0x6C8C writable |

---

## IOMMU Impact

| Setting | SCRATCH | KIQ_BASE | CTX_CNTL | HQD_ACTIVE |
|---------|---------|----------|----------|------------|
| IOMMU ON | 0x00000000 | 0x00000000 | 0x00000000 | 0x0000FFF0 |
| IOMMU OFF | 0x4D585042 | 0x7E522000 | 0x010CA88D | 0x0000FFF0 |

**IOMMU must stay OFF for GPU to function.**

---

## Next Steps

### Immediate
1. **DON'T replace PT_BASE** — use BIOS page tables as-is
2. **Use BIOS ring (0x7E522000)** — it's already mapped in BIOS page tables
3. **Or add mapping to existing PML4** — walk BIOS page tables, add entry for new ring

### After PM4 Execution Confirmed
4. Build actual GPU command submission pipeline
5. Test compute shader execution
6. Test vertex/draw commands

### Long Term
7. ACO shader compilation (DXBC/SPIR-V → GFX10 ISA)
8. D3D9/D3D11 via IOCTL path
9. OpenGL ICD (Mesa radeonsi port)

---

## Build Environment

- **VS2022 Professional** — D:\Program Files\Microsoft Visual Studio\2022\Professional
- **WDK** — D:\Program Files (x86)\Windows Kits\10\10.0.26100.0
- **Test signing** — `bcdedit /set testsigning on`
- **IOMMU** — DISABLED in BIOS
- **VBS/Hypervisor** — DISABLED (`bcdedit /set hypervisorlaunchtype off`)

---

## Repository

- **GPU Driver**: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver
- **PSP Driver**: https://github.com/Keshas-dev/AMD-BC-250-PSP-Windows-Driver

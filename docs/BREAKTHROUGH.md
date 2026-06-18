# BC-250 Breakthrough: GCVM, Firmware & PM4 (2026-06-18)

## TL;DR
- **GCVM is ALIVE** — previous "dead" conclusion was WRONG (wrong offset calculation)
- Correct formula: `BAR5_offset = GC_BASE(0x1260) + Linux_DWORD_offset * 4`
- GCVM_CONTEXT0_CNTL (0x0B460) = 0x010CA88D, **WRITABLE**
- GCVM_CONTEXT0_PT_BASE_LO (0x0B608) and HI (0x0B60C) — **writable**
- **PTE bug found**: SYSTEM bit (bit 1) was missing from PTE flags — 0x61→0x63
- **CP firmware exists**: cyan_skillfish2 ME/PFP/CE binaries in firmware/ directory
- **LOAD_CP_FW IOCTL** (0x80000BD4) implemented — can load firmware via MMIO
- **Next**: Load ME+PFP firmware → test PM4 with corrected PTE flags

---

## GCVM Register Offsets (CORRECT, 2026-06-18)

### Correct GCVM offset calculation
Formula: `BAR5_offset = GC_BASE(0x1260) + Linux_DWORD_offset * 4`

Example: Linux GCVM_CONTEXT0_CNTL at DWORD offset 0x2D18:
- BAR5 offset = 0x1260 + 0x2D18 * 4 = 0x1260 + 0xB460 = 0x0B460 (byte offset)

### Correct GCVM Context Registers
| Register | Linux DWORD | BC-250 BAR5 | Status |
|---|---|---|---|
| GCVM_CONTEXT0_CNTL | 0x2D18 | **0x0B460** | ✅ ALIVE! value=0x010CA88D, WRITABLE |
| GCVM_CONTEXT0_PT_BASE_LO | 0x2D82 | **0x0B608** | ✅ ALIVE! WRITABLE (was 0) |
| GCVM_CONTEXT0_PT_BASE_HI | 0x2D83 | **0x0B60C** | ✅ ALIVE! WRITABLE (was 0) |
| GCVM_L2_CNTL | 0x2CD8 | **0x0B360** | ✅ ALIVE! value=0x013C67B8 |

### Previous WRONG offsets (DO NOT USE)
| Old Offset | What we thought | Reality |
|---|---|---|
| 0x2840-0x2987 | GCVM registers | All 0xFFFFFFFF (wrong calculation) |
| 0x1A00 range | MMHUB VM | Dead |
| 0x9B00-0x9B90 | hw_extra VM | Read-only zeros |

### GCVM_CONTEXT0_CNTL bit fields (0x010CA88D)
- Bit 0 = 1: L1 TLB enabled
- Bit 2 = 1: ?
- Bit 3 = 1: ?
- Bit 7 = 1: ?
- Bit 10 = 1: ?
- Bit 11 = 1: ?
- Bit 12 = 1: ?
- Bit 16 = 1: ?
- Bit 24 = 1: ?

---

## PTE Format (RDNA2/GFX10) — CRITICAL FIX

### AMD GPU PTE flags (from Linux amdgpu_vm.h)
```c
AMDGPU_PTE_VALID    = bit 0  (0x01)
AMDGPU_PTE_SYSTEM   = bit 1  (0x02) — system memory, not VRAM
AMDGPU_PTE_SNOOPED  = bit 2  (0x04) — CPU coherent
AMDGPU_PTE_READABLE = bit 5  (0x20)
AMDGPU_PTE_WRITEABLE = bit 6 (0x40)
```

### PDE flags (pointing to next page table level)
- VALID(bit0) | SYSTEM(bit1) = `0x03` ✅ correct

### PTE flags (leaf, 4KB page mapping)
- **WRONG**: `0x61` = VALID | READABLE | WRITEABLE (missing SYSTEM!)
- **CORRECT**: `0x63` = VALID | SYSTEM | READABLE | WRITEABLE

Without SYSTEM bit, GPU treated system RAM addresses as VRAM → translation failed silently.

---

## GCVM Page Table Setup (in GPU_KIQ_TEST)

### Hierarchy: 4-level (PML4 → PDP → PD → PT)
- Each level: 512 entries × 8 bytes = 4KB per page
- All pages allocated below 4GB via MmAllocateContiguousMemorySpecifyCache

### Identity mapping (VA=PA)
- Ring buffer physical address decomposed into page table indices:
  - PML4[0], PDP[pdpIdx], PD[pdIdx], PT[ptIdx]
- PDE entries link levels: PML4→PDP, PDP→PD, PD→PT
- PTE entry maps ring buffer page: VA=PA with flags 0x63

### Sequence in GPU_KIQ_TEST
1. Allocate PML4/PDP/PD/PT pages below 4GB
2. Fill identity mapping entries
3. Write GCVM_CONTEXT0_PT_BASE = PML4 physical address
4. Set bit 0 of GCVM_CONTEXT0_CNTL (enable L1 TLB)
5. Halt ME, deactivate queue, program HQD registers
6. Activate queue, resume ME
7. Write PM4 to ring, set WPTR
8. Wait 50ms, check SCRATCH

---

## CP Firmware — Direct MMIO Load (2026-06-18)

### LOAD_CP_FW IOCTL (0x80000BD4)
Input: struct { FwType, FwSize } + firmware blob appended
Output: struct { Result, UcodeVersion }

Firmware header (44 bytes):
- [0] total_size, [1] hdr_size, [2-3] version, [4] ucode_version
- [5] ucode_size, [6] ucode_offset, [7] checksum
- [8] data_offset, [9] jt_offset_dws, [10] jt_size_dws

### Upload sequence
1. Halt ME+PFP+CE (set bits 28-30 of ME_CNTL at 0x4A74)
2. Set IC_BASE for target engine (ME: 0x17370-0x17378)
3. Upload Jump Table via UCODE_DATA registers
4. Write ucode_version to UCODE_ADDR to commit
5. Unhalt the loaded engine

### Cyan Skillfish2 firmware files
| File | Size | Ucode Version | Notes |
|---|---|---|---|
| cyan_skillfish2_me.bin | 263,424 | 99 | ME ucode, JT=65536+128 DWs |
| cyan_skillfish2_pfp.bin | 263,424 | ? | PFP ucode |
| cyan_skillfish2_ce.bin | 263,296 | ? | CE ucode |
| cyan_skillfish2_mec.bin | 268,592 | ? | MEC (compute) ucode |
| cyan_skillfish2_rlc.bin | 25,344 | ? | RLC ucode |
| cyan_skillfish2_sdma.bin | 33,792 | ? | SDMA ucode |

---

## PM4 Submission Status (2026-06-18)

### Previous PM4 tests (before fixes)
- GCVM was at wrong offsets → all registers read 0xFFFFFFFF
- PTE flags missing SYSTEM bit (0x61 instead of 0x63)
- CP firmware not loaded
- **Result**: SCRATCH unchanged (0x4D585042), WPTR unchanged

### What's been fixed
1. ✅ GCVM offsets corrected — registers alive and writable
2. ✅ PTE flags corrected — SYSTEM bit added (0x61→0x63)
3. 🔄 CP firmware loading — LOAD_CP_FW implemented, ready to test

### What's still unknown
- Whether the GCVM page table format is exactly right (might need SNOOPED bit)
- Whether IC_BASE DMA works on BC-250
- Whether ME/PFP firmware loading actually makes PM4 work
- GCVM_CONTEXT0_CNTL exact bit field meanings

---

## Other Register Blocks

### HDP Block (0x0500-0x05FF) — 61 writable registers
- MC_VM_FB_LOC_TOP (0x0524) ✅ writable
- MC_VM_AGP_BASE (0x0528) ✅ writable

### CP Ring Registers
- GFX RING0_BASE_LO (0xDA60): **READONLY** — BIOS sets ring base
- COMPUTE_BASE_LO (0xDB60): **READONLY** — same
- KIQ_BASE_LO (0xE060): **WRITABLE** — KIQ works

### GRBM_GFX_INDEX Queue Select
- **Does NOT work on BC-250** — all 16 queues return identical values
- Cannot access per-queue HQD registers independently

### CP Fence/Doorbell (0x3AD8-0x3AEC)
- 0x02A8xxxx values — doorbell aperture addresses, set by BIOS

---

## BC-250 Linux IP Map
| Block | Base | IP Version |
|---|---|---|
| GC | 0x1260 | 10.1.3 |
| NBIO | 0x0000 | 2.1.1 |
| HDP | 0x0F20 | - |
| MMHUB | 0x1A000 | - |
| DF | 0x7000 | - |
| OSSSYS | 0x10A0 | - |
| MP0/PSP | 0x16000 | 11.0.8 |
| THM | 0x16600 | - |
| SMUIO | 0x16800/0x16A00 | - |
| CLK | 0x16C00+ | - |
| UMC0 | 0x14000 | - |
| FUSE | 0x17400 | - |

---

## Key Next Steps
1. **Load CP firmware** (ME+PFP) via LOAD_CP_FW — test tool ready
2. **Test PM4** with firmware loaded + corrected PTE flags
3. If PM4 works → build full command submission path
4. If PM4 fails → try PTE with SNOOPED bit (0x67), try different GCVM configs
5. Investigate GCVM_CONTEXT0_CNTL bit fields

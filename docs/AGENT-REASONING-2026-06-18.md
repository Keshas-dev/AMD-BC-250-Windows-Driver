# BC-250 Agent Reasoning Log (2026-06-18)

## What I figured out today and how

### 1. GCVM was NOT dead — wrong offsets

**Previous conclusion (WRONG)**: "ALL GCVM registers are 0xFFFFFFFF (power-gated)"
- We were checking offsets 0x2840-0x2987 and 0x1A00
- All read as 0xFFFFFFFF → concluded GCVM dead

**How I found the error**:
- Linux kernel uses `GCVM_offset` relative to GC base, not BAR5 byte offset
- Linux `cyan_skillfish_ip_offset.h` shows GCVM_CONTEXT0_CNTL at DWORD offset 0x2D18
- Formula: `BAR5_byte_offset = GC_BASE(0x1260) + Linux_DWORD_offset * 4`
- 0x1260 + 0x2D18*4 = 0x1260 + 0xB460 = **0x0B460**
- Previous code used wrong calculation: 0x1260 + 0x2D18 = 0x3F78 → wrong
- Or used raw Linux DWORD as byte offset: 0x2D18*4 = 0xB460 → also wrong (missing GC_BASE)

**The fix**: At 0x0B460, GCVM_CONTEXT0_CNTL = 0x010CA88D (alive!), writable with w=1

**Lesson**: Linux register offsets for AMD GPUs are in DWORDs relative to IP base, not byte offsets from BAR5. Need to multiply by 4 and add GC_BASE.

### 2. PTE flags were wrong — missing SYSTEM bit

**Previous PTE**: 0x61 = VALID(bit0) | READABLE(bit5) | WRITEABLE(bit6)
**Correct PTE**: 0x63 = VALID(bit0) | SYSTEM(bit1) | READABLE(bit5) | WRITEABLE(bit6)

**How I found this**:
- Looked at Linux `amdgpu_vm.h` for PTE flag definitions
- AMDGPU_PTE_SYSTEM = bit 1 = 0x02
- Our ring buffer is allocated in system RAM (via MmAllocateContiguousMemory)
- Without SYSTEM bit, GPU memory controller treats the address as VRAM, not system memory
- Page table translation succeeds but the memory access fails silently

**Why this matters**: On AMD GPUs, the PTE SYSTEM bit tells the memory controller whether to route through the PCIe bus (system memory) or access local VRAM. Without it, the GPU tries to read from a VRAM address that doesn't exist.

### 3. CP firmware needs to be loaded

**Previous assumption**: BIOS loads all GPU firmware at boot
**Reality**: BIOS loads PSP firmware and basic display, but CP ME/PFP microcode may not be loaded

**How I found this**:
- GPU_KIQ_TEST programs all HQD registers, activates queue, writes PM4
- WPTR gets set to 8 but GPU never reads it (WPTR stays 8)
- ME_CNTL halt/unhalt works, but ME might not have firmware to execute
- LOAD_CP_FW IOCTL was already implemented in the driver but never tested
- Firmware files exist: cyan_skillfish2_me.bin (263KB), cyan_skillfish2_pfp.bin (263KB)

**The plan**: Load ME firmware first, then PFP, then try PM4 again

### 4. GRBM_GFX_INDEX queue select is broken on BC-250

**Finding**: Writing different queue indices to GRBM_GFX_INDEX always returns identical values
**Implication**: Cannot access per-queue HQD registers individually
**Workaround**: Use broadcast mode (GRBM_INDEX=0xE0000000) or accept that all queues share state

### 5. GFX/Compute ring bases are READONLY

**Finding**: RING0_BASE_LO at 0xDA60 and 0xDB60 cannot be written
**Implication**: BIOS controls the main GFX/compute ring, not the OS
**Workaround**: Use KIQ ring (KIQ_BASE_LO at 0xE060 is writable)

---

## What I want to do next

### Immediate (next reboot)
1. **Install rebuilt driver** (with PTE fix 0x61→0x63)
2. **Run load-cp-fw.exe** which does:
   - INIT_HARDWARE (NBIO_MAP)
   - Load ME firmware via LOAD_CP_FW
   - Load PFP firmware via LOAD_CP_FW
   - Run GPU_KIQ_TEST (with corrected PTE flags)
3. **Check results**:
   - If SCRATCH = 0xCAFEBABE → PM4 works! We can submit commands to GPU
   - If SCRATCH unchanged → need to investigate further

### If PM4 works
1. Build a proper command submission path
2. Try basic GPU compute (shader execution)
3. Investigate MMHUB VM registers for memory mapping
4. Try to get 3D rendering working

### If PM4 still fails
1. Try adding SNOOPED bit to PTE (0x67 instead of 0x63)
2. Check if GCVM_CONTEXT0_CNTL needs additional configuration
3. Investigate whether IC_BASE DMA actually works on BC-250
4. Try writing PM4 directly to ring via CPU and only use WPTR to kick GPU
5. Look at MMHUB VM registers (0x1B478-0x1B548) — all zero currently, may need configuration

### Longer term
1. Get DXGKRNL working (real WDDM miniport) — this handles GPU VM automatically
2. Or bypass GPU VM entirely if possible (identity mapping might work for system memory)
3. Investigate what BIOS does during POST to set up the GPU
4. Try to read BIOS firmware for clues about VM setup

---

## Key technical insights

### AMD GPU register addressing on BC-250
- Linux IP offsets are DWORD offsets from IP base
- BAR5 byte offset = GC_BASE(0x1260) + Linux_DWORD_offset * 4
- Some registers are per-engine (selected via GRBM_INDEX)
- Some registers are broadcast (GRBM_INDEX = 0xE0000000)
- BC-250 queue select doesn't work — all queues appear identical

### AMD GPU page tables (GFX10/RDNA2)
- 4-level hierarchy: PML4 → PDP → PD → PT
- 512 entries per level × 8 bytes = 4KB per page
- PTE flags: VALID(bit0) | SYSTEM(bit1) | SNOOPED(bit2) | READABLE(bit5) | WRITEABLE(bit6)
- SYSTEM bit is critical for system memory pages

### CP firmware loading
- ME/PFP/CE firmware loaded via UCODE_ADDR/UCODE_DATA registers
- Jump Table uploaded first, then ucode version written to commit
- IC_BASE registers tell DMA engine where firmware lives in memory
- Need to halt engine before loading, unhalt after

### PM4 command submission
- Write PM4 packets to ring buffer (virtual address)
- Update WPTR register to tell GPU how many DWORDs to process
- GPU CP (Command Processor) reads from ring, executes commands
- Without working page tables, GPU cannot access ring buffer memory

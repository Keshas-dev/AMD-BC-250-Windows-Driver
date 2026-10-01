# BC-250 root-cause analysis — Linux evidence, ROCm research, and our own bugs

Date assembled: 2026-10-01
Scope: findings that **change the project's direction**. Source-attributed, with
what is measured vs. what is inferred clearly separated.

This document supersedes several conclusions in `AGENTS.md`. Where they disagree,
this file is newer and has direct measurement behind it.

---

## 0. TL;DR

1. The GPU **is not fused or BIOS-locked**. `SPI_PG`/`RLC_PG` come pre-opened
   (`0x1f`) from the VBIOS at POST on P5.00 boards.
2. The real blocker is a **translation defect below the page table**: the GPU's
   TLB is never invalidated, so the address cache (ATC) keeps serving the
   *previous* generation's mapping. The page tables themselves are correct.
3. A workaround **exists and is validated** (rebuild the runlist instead of
   invalidating), but it depends on KFD internals we do not have.
4. We have **two concrete code bugs** in the firmware load path that are real and
   independent of the above.
5. Reliable GPU compute on BC-250 is therefore a **multi-part problem**, not a
   single unlock bit.

---

## 1. Safety: what must never be read

| Action | Result | Evidence |
|---|---|---|
| `cat /sys/kernel/debug/dri/*/amdgpu_regs` | **hangs the machine** | `test-tools/linux/New/testas.txt:50`; independently in `docs/08`, `docs/01`, `docs/21` |
| `amdgpu_iomem` | unreadable on CachyOS | user report, `linux3/iomem.txt` = 0 B |
| debugfs path | is **`/sys/kernel/debug/dri/1/`**, not `/0/` | `testas.txt:48` — `dri/0/amdgpu_regs` does not exist |

`amdgpu_regs` walks **every** IP block and performs every read side-effect at
once. Our own safety rule already said roughly a third of SMN registers have read
side effects; this exposes all of them concurrently.

`docs/08` states it plainly: *"Do **not** read them from userspace via
`amdgpu_regs`. That was tried and **hung the machine**."*

### Our own read-only probe also hung the board

`output\mmhub-vm-probe.exe` performs **only** `IOCTL_AMDBC250_READ_REG` — it
contains no `WRITE_REG` call and no SMU message. It still hung the machine.

Conclusion: **an unvalidated MMIO read address is not safe on this chip even for
reads.** Address validation must happen before any read, not during it. This is
our mistake, not the user's.

---

## 2. Linux init order and addresses (measured)

From `test-tools/linux/New/linux/dmesg.txt`:

| t (s) | step |
|---|---|
| 6.588476 | `vm size is 262144 GB, 4 levels, block size is 9-bit, fragment size is 9-bit` |
| 6.588486 | `VRAM: 512M 0x000000F400000000 - 0x000000F41FFFFFFF` |
| 6.588490 | `GART: 512M 0x0000000000000000 - 0x000000001FFFFFFF` |
| 6.588939 | `PCIE GART of 512M enabled (table at 0x000000F41FE00000)` |
| 6.610512 | `reserve 0x400000 from 0xf41f800000 for PSP TMR` |
| 6.646822 | `SMU is initialized successfully!` (88.6.0) |

**Order matters: GART → TMR → SMU.**

### The GART/TMR layout rule (validated on two VRAM sizes)

The positions are *distances from VRAM top*, not fixed offsets from the base:

| VRAM | GART table | PSP TMR |
|---|---|---|
| 512 MB (CachyOS capture) | `0xF41FE00000` | `0xF41F800000` |
| 6 GB (ROCm 6313 reporter) | `0xF57FE00000` | `0xF57F800000` |

```
GART = vramEnd - 0x200000
TMR  = vramEnd - 0x800000
```

### GART table and TMR dumps

| Artefact | File | Verdict |
|---|---|---|
| GART table, 1 MB = 131072 × 8 B | `linux3/gart.bin` | **structure confirmed** — low 12 bits constant `0x073`, field above bit 11 increments by 1 per entry (4 KB granularity) |
| PSP TMR, 4 MB | `amdgpu_top/tmz.bin` | **1048576 dwords, all `0xFFFFFFFF`** — a single unique value |
| GFX MQD (2 KB) | `linux/New/linux/amdgpu_mqd_gfx_0.0.0` | **empty** — 20 non-zero bytes |
| GFX / KIQ / SDMA rings | `linux/New/linux/amdgpu_ring_*` | **idle** — KIQ is 4095 NOPs |

> **Correction:** an earlier `dd` instruction in this session used `bs=1M skip=511`
> for the GART table. `0x1FE00000` is **510 MB**, so that read landed 1 MB high
> and returned unrelated VRAM contents. `skip=510` is correct. The TMR read
> (`skip=504`) was correct.

### The TMR is inert — it is not the blocker

The TMR is 4 MB of `0xFF` on a fully working Linux system, with no live index
counter. The PSP writes nothing to it at idle.

**Therefore TMR setup is a precondition for a clean SETUP_TMR return, not a
path to execution.** Commit `d9f8326` corrects the address (which was genuinely
wrong — 224 MB low) but will not by itself make the GPU run anything. It should
not be prioritised as though it would.

---

## 3. The GART enable sequence (from Linux source)

`mmhub_v1_0_gart_enable()` (`mmhub_v1_0.c:376-385`) calls, in order:

```
init_gart_aperture_regs      → VM_CONTEXT0_PAGE_TABLE_BASE/START/END
init_system_aperture_regs    → MC_VM_AGP_*, MC_VM_SYSTEM_APERTURE_*, VM_L2_PROTECTION_FAULT_*
init_tlb_regs                → MC_VM_MX_L1_TLB_CNTL
init_cache_regs              → VM_L2_CNTL / CNT L2 / CNT L3 / CNT L4
enable_system_domain         → VM_CONTEXT0_CNTL          ⭐
disable_identity_aperture    → VM_L2_CONTEXT1_IDENTITY_APERTURE_*
setup_vmid_config            → VM_CONTEXT1..15_CNTL
program_invalidation         → VM_INVALIDATE_ENG0_SEM/REQ/ACK
```

`mmhub_v1_0_enable_system_domain()` (`:199-209`) — this is AGENTS.md hypothesis B,
now confirmed from source:

```c
tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, ENABLE_CONTEXT, 1);
tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, PAGE_TABLE_DEPTH, 0);
tmp = REG_SET_FIELD(tmp, VM_CONTEXT0_CNTL, RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
```

`mmhub_v1_0_init_tlb_regs()` (`:137-155`):

```
MC_VM_MX_L1_TLB_CNTL: ENABLE_L1_TLB=1, SYSTEM_ACCESS_MODE=3,
                       ENABLE_ADVANCED_DRIVER_MODEL=1,
                       SYSTEM_APERTURE_UNMAPPED_ACCESS=0,
                       MTYPE=MTYPE_UC, ATC_EN=1
```

`gmc_v10_0.c:737-739` — GART sizing, confirmed:

```c
adev->gart.table_size = adev->gart.num_gpu_pages * 8;         /* 131072*8 = 1 MB */
adev->gart.gart_pte_flags = AMDGPU_PTE_MTYPE_NV10(0ULL, MTYPE_UC) |
                            AMDGPU_PTE_EXECUTABLE;
```

`gmc_v10_0.c:714` — IP 10.1.3 falls to `default:` → `amdgpu_gmc_set_gart_size(adev, SZ_512M)`.

`gmc_v10_0.c:698` — **`aper_base = gfxhub->get_mc_fb_offset()`**, not BAR0. Our code
uses `FbPhysicalBase` with a `0xC0000000` fallback, which is wrong for an APU.

### MMHUB register offsets — verified, not guessed

From `drivers/gpu/drm/amd/include/asic_reg/mmhub/mmhub_1_0_offset.h`, with MMHUB
base `0x1A000` dwords (= BAR5 `0x68000`) from the board's own IP discovery:

| Register | MMHUB | BAR5 |
|---|---|---|
| `VM_L2_CNTL` | `0x0680` | `0x69A00` |
| `VM_L2_CNTL2/3/4` | `0x0681/0682/0697` | `0x69A04/08/18` |
| **`VM_CONTEXT0_CNTL`** | `0x06c0` | **`0x69B00`** |
| `VM_INVALIDATE_ENG0_SEM` | `0x06d1` | `0x69B44` |
| `VM_INVALIDATE_ENG0_REQ` | `0x06e3` | `0x69B8C` |
| `VM_INVALIDATE_ENG0_ACK` | `0x06f5` | `0x69BD4` |
| `VM_CTX0_PT_BASE_LO32` | `0x072b` | `0x69CAC` |
| `MC_VM_AGP_TOP/BOT/BASE` | `0x082e/082f/0830` | `0x6A0B8/BC/C0` |
| **`MC_VM_MX_L1_TLB_CNTL`** | `0x0833` | **`0x6A0CC`** |

---

## 4. Our GART/VM offsets are wrong

| Register | Linux (correct) | `inc/amdbc250_dream_hw.h` | Δ |
|---|---|---|---|
| `MC_VM_AGP_BASE` | `0x6A0C0` | `0x9528` | `0x0E74` |
| `MC_VM_AGP_TOP` | `0x6A0B8` | `0x952C` | `0x0E78` |
| `MC_VM_AGP_BOT` | `0x6A0BC` | `0x9530` | `0x0E74` |
| `VM_CONTEXT0_CNTL` | `0x69B00` | absent | — |
| `MC_VM_MX_L1_TLB_CNTL` | `0x6A0CC` | absent | — |

`inc/amdbc250_dream_hw.h:615` already annotates `MMHUB_VM_CONTEXT0_CNTL 0x1A00`
as *"MREA, NOT VM"* — correctly identified as not the VM register, but the real
one at `0x69B00` is never added.

`MC_VM_SYSTEM_APERTURE_LOW/HIGH` at `0x9540/0x9544` are likewise unverified.

**`DreamV3GartInitialize()` in `src/kmd/amdbc250_dream_vm.c` writes to a block
that is not the MMHUB VM block.** It must not be run in its current form.

---

## 5. WGP registers are opened by the VBIOS — our unlock quest was misdirected

`docs/16-wgp-registers-vem-do-vbios.md`, measured with a read-only in-driver
patch at three points of `gfx_v10_0_hw_init()`, warm boot **and** full cold boot:

```
after constants_init   CC=0xffe00000  SPI=0x0000001f  RLC=0x0000001f
after rlc_resume       CC=0xffe00000  SPI=0x0000001f  RLC=0x0000001f
after cp_resume        CC=0xffe00000  SPI=0x0000001f  RLC=0x0000001f
```

- `SPI_PG_ENABLE_STATIC_WGP_MASK` = `0x1f` **from POST**, survives cold boot.
- `RLC_PG_ALWAYS_ON_WGP_MASK` = `0x1f` **from POST**. Writing it is a **no-op**.
- `CC_GC_SHADER_ARRAY_CONFIG` reads `0xffe00000` — a **fuse shadow**. Writes
  change enumeration, reads return the fuse. This is why `active_cu_number`
  can be 40 while the register reads the mask.

**Critical caveat: this was measured on BIOS P5.00 (05/03/2022).** We are on
BIOS 3.00. Our readings differ (`SPI=0x00000000`, `RLC=0xFFFFFFFF`), so the
difference is plausibly the **BIOS version**, not the OS.

> **This retires the "SPI_PG is SOS-locked" verdict as a hardware fact.** It may
> be a BIOS-version artifact. It also explains why our UEFI probe wedged the SMU:
> we were writing a register that was already at its target value.

`docs/16` also notes: `umr` **fails to read exactly these offsets** on BC-250,
which is why this data was never available from userspace.

---

## 6. The actual root cause: the TLB is never invalidated

From `data/data-ptwalk-walks.txt` (14 boots) and `docs/17`, `docs/21`, `docs/22`.

### The page table is correct

```
[PTB] entry[1] = 0x004000045c8004e1  -> PA 0x45c800000
      flags: VALID READ WRITE PDE_PTE(big page)  frag=9 mtype=0
>>> VEREDITO: a PTE esta CORRETA.
```

Decoded: `bit0` valid, `bit1` read, `bit2` write, `bit62` `PDE_PTE`, address
`0x45c800000`. Geometry measured: **3 levels**, `root_level=1`, `block_size=9`,
`frag=4` — *not* the 4-level page table the driver advertises in dmesg.

### The ATC is never tagged

```
vmid  0  ATC=INVAL  pasid=0x0  pd=0x0
...
vmid 15  ATC=INVAL  pasid=0x0  pd=0x0
>>> VMIDs whose ATC entry matches our PASID: 0
```

`docs/21`: **80 VMID lines across 5 dumps, every one invalid. Zero valid
entries.** Independently reproduced on separate hardware.

Cause: `gmc_v10_0_flush_gpu_tlb_pasid()` looks up the VMID via
`mmATC_VMID*_PASID_MAPPING`, **a register that on gfx10 under HWS is never
written.** 20 of 20 flushes matched zero VMIDs.

### Forcing invalidation hangs the board

Both routes (KIQ and direct MMIO) stall the translation unit and reset the GPU.
`0x28b4`/`0x28c6` — the same `vm_inv_eng0_req`/`_ack + eng_distance*17` pair our
`gmc_v10_0.c:235` reads — never acknowledge.

The L2 page-table cache is exonerated: **`FORCE_MISS` does not fix the
corruption** (measured, 6/10, `docs/22`). By elimination, the TLB itself is the
remaining candidate.

### The validated workaround: rebuild the runlist

`docs/23` found the entry point:

| action on a page that is 20/20 wrong | result |
|---|---|
| process re-reads it, 60× | still 20/20 |
| process touches 32 new pages | still 20/20 |
| process runs a compute dispatch | still 20/20 |
| **another process accesses the GPU** | **0/20 — repaired** |
| another process maps the BO but does not access | still 20/20 |

A process cannot clear its own bad translation. **A runlist change can** — the
scheduler preempts queues and reassigns VMIDs, which must invalidate.

`docs/24`, 36 runs counterbalanced within one boot:

```
stock     13/18 dirty   72%
runlist    0/18 dirty    0%      Fisher exact p = 3.7e-06
```

Mechanism confirmed (`tools/pa_anterior.py`, 15/15 pages, 3 runs, 0 exceptions):
**the GPU translates through the previous generation's mapping of that VA**, and
the fault is born at the `hipFree` whose invalidation never happened.

### Why this does not port to us directly

The fix is `kfd_bc250_flush_by_runlist()` in KFD, called from the unmap ioctl
behind `dqm_lock`. It needs:

- `dqm` (device queue manager) and `dqm_lock`
- a VMID allocator
- hardware queue preemption (`execute_queues_cpsch`)

**We are a WDM IOCTL driver, not a WDDM miniport.** We have none of these. Porting
means writing a GPU compute scheduler from scratch — a different task, not a
smaller version of the same one.

---

## 6.5 The gate is closed at POST, measured with a bank selected (2026-10-01)

This supersedes the "unconfirmed" status in §5. It is now a **measurement**, not
an inference.

`output\wgp-bank-probe.exe` drives a new driver IOCTL
(`IOCTL_AMDBC250_WGP_BANK_PROBE`, packed `0x80000B50`) that saves
`GRBM_GFX_INDEX`, walks the four gfx10 shader-array banks, and restores it.
Doing the selection in the driver is required because these registers are
per-bank, and a user-mode tool cannot select a bank without performing the
`GRBM_GFX_INDEX` write that this driver documents as display-fatal.

Result, all four banks:

| Register | 0x9C1C-era value | Ours | `docs/16` (BIOS P5.00) |
|---|---|---|---|
| `SPI_PG_ENABLE_STATIC_WGP_MASK` `0x5C3C` | `0x1f` | **`0x00000000`** | `0x1f` from POST |
| `CC_GC_SHADER_ARRAY_CONFIG` `0x9C1C` | `0xffe00000` | `0x00000000` | `0xffe00000` |
| `CC_GC_SHADER_ARRAY_CONFIG` `0x529C` | n/a | `0x00000000` | n/a |

Control values were all correct, which is what makes this trustworthy:

```
GPU_ID            = 0x9FFF9700        ok
MmioVirtualBase   = 0x012A6000  MmioSize = 0x80000
HardwareInitialized = 1   ReadOk = 1
GRBM_GFX_INDEX    = 0xBA062100  saved and restored (restored = 1)
GrbmEcho          == BankSel for all four banks
```

**Conclusion.** The 40 CU gate is not an ACL, not SOS, and not reachable from
UEFI, SMU or PSP. Our board does not come out of POST with the shader array
enabled. Every WGP route tried over six months was aimed at the wrong layer.

`CC` is zero at `0x9C1C` as well, so the earlier observation that `0x9C1C` is
partially writable only described a leftover from our own write. At boot it
reads zero. `mm 0x226F` behind `0x9C1C` is not in `gc_10_1_0_offset.h` either,
so that address remains unexplained.

## 6.6 Two driver bugs found by making a probe return data

**`bytesReturned` was never set, so no output came back.** With `METHOD_BUFFERED`
the I/O manager copies exactly `Irp->IoStatus.Information` bytes. A handler that
fills `SystemBuffer` but leaves `bytesReturned = 0` returns success and the user
buffer is never updated. This is indistinguishable from "the case was never
reached", which is how it presented: every field read as zero, including
`GPU_ID`.

The discriminator is a **sentinel**, not a zero fill. Filling the user buffer
with `0xDEADBEEF` before the call distinguishes the two cases in a single run.
Pre-filling with zeros cannot, which is why the first run looked like a clean
measurement of a gated register rather than an empty buffer.

Every "the register reads 0" claim made before this was fixed is void, including
the `SPI_PG = 0` reading that §6.5 now re-measured properly.

**Header and driver disagree on IOCTL numbering.** The header builds codes with
`IOCTL_INDEX 0x270` (for example `IOCTL_AMDBC250_READ_REG` = `0x80000B88`),
but 56 `case` labels in the driver are hand-written literals on the older
`0x200`-based scheme (`0x80000988` and so on). Tools happen to work because
each tool targets whatever its own build resolves and the driver happens to
handle both, but the two are not consistently paired: some cases carry the
legacy literal only, some the header value only, and at least one
(`GCVM_PT_SETUP`, `0x8000098C`) matches no code this `IOCTL_INDEX` can produce,
making it dead.

This is a bug factory. Any new tool written against the header silently fails
with `ERROR_INVALID_FUNCTION`. Either convert every case to the macro, or set
`IOCTL_INDEX` back to `0x200` and fix the tools - but not leave it half-mixed.


`docs/29`, measured with `n=3`, deterministic:

| firmware | bytes copied | runs |
|---|---|---|
| `cyan_skillfish2` `0x34` | **0 of 4194304**, signal never drops, 5 s timeout | 3 |
| `navi12` `0x2c` | **4194304 / 4194304 in 0.04 s** | 3 |

Per engine, `hsa_amd_memory_async_copy_on_engine()`: with `0x34` the signal never
drops in 8 s and 0 bytes move; with `0x2c` it drops in **0.005 s**, data correct.

Headers are structurally identical, so the swap is safe:

| | `cyan_skillfish2` | `navi12` |
|---|---|---|
| file size | 33792 | 33792 |
| IP version | 5.0 | 5.0 |
| ucode size / offset | 33536 / 256 | 33536 / 256 |
| ucode version | `0x34` | `0x2c` |

**No signature checking occurs** — empirically proven, since navi12's blob is
signed for navi12 and loads anyway.

The broken build never touches 24 registers that both working builds do: three
registers × eight groups at stride `0x180`, matching
`mmSDMA0_RLC0_RB_CNTL` (`0x140`) → `mmSDMA0_RLC1_RB_CNTL` (`0x1a0`). Eight RLC
queues = user queues. The driver has configured 8 user queues on this chip since
2018; the blob AMD published three years later does not drive them.

There is no other version to fall back to — one commit, 2021-11-12, byte-identical
in `linux-firmware` today. `cyan_skillfish` v1 (without the `2`) was never released.

---

## 8. A working configuration exists

`config/bc250-rocm.sh`:

```sh
export GPU_PINNED_MIN_XFER_SIZE=16384   # without this: hangs loading the model
export HSA_ENABLE_SDMA=0                # SDMA broken for host<->VRAM
export TORCH_BLAS_PREFER_HIPBLASLT=0    # hipBLASLt has no gfx1013 kernels
```

With `bc250-flush-tlb-by-runlist.patch` enabled: **16 runs, 0 corrupted**.
The serialization flags were measured and **do nothing**:

```
with the flags:    3 runs, 2 corrupted, 35 tensors clobbered
without them:      3 runs, 3 corrupted, 25 tensors clobbered, 1 crash
```

`proof/` contains real generated images (`sd15-gpu-vae-512x512.png`,
`z-image-turbo-512x512.png`, `red-cube-256x256.png`). **It does work.**

Open items they state themselves: heavy-load ComfyUI untested with the patch,
preemption under sustained load untested, performance unmeasured.

---

## 9. ROCm 6313 — unstable on mainline too

`https://github.com/ROCm/legacy-rocm-build/issues/6313` (opened 2026-07-25,
assigned `amd-nicknick`, label `status: triage`, still open):

```
amdgpu_vm_bo_update failed
update_gpuvm_pte() failed
Failed to map peer:0000:01:00.0 mem_domain:4
The cp might be in an unrecoverable state
[drm] device wedged, but recovered through reset
```

The reporter suspected *"a power or memory management bug, since this is an APU
with shared system memory"* — consistent with the ATC finding above. ROCm 5.2
worked; 7.2.x freezes. A Google search is not a stable reference.

Note the reporter's VM base is `0xF57F000000` (6 GB UMA) yet the GART/TMR
distances from top are identical — see §2.

---

## 10. Bugs in our own code — confirmed, actionable, independent of the above

### 10.1 Firmware is never loaded on the active code path

`HwInitExtended=1` is the default, which routes through
`src/kmd/amdbc250_dream_hw_init_extended.c`. That file calls **only**
`DreamV3LoadPspFirmware()`. **`DreamV3LoadAllFirmware()` is never called from it.**

Callers found: `amdbc250_dream_hw_init.c:274` (the non-extended path) and
`amdbc250_dream_kmd.c:6983` (GPU_KIQ_TEST). Neither is on the default path.

### 10.2 The firmware loader contradicts itself and abandons the host path

`src/kmd/amdbc250_dream_fw_load.c`:

- `:147` — *"uploads via IC_BASE DMA, matching Linux AMDGPU_FW_LOAD_DIRECT"*
- `:231` — *"We deliberately do **NOT** use the host IC_BASE DMA path"*

`DreamV3LoadSingleFirmware()` is referenced only in that comment — **the function
does not exist**.

### 10.3 So no CP or SDMA firmware is ever loaded

The surviving path is `Amdbc250PspKiqLoadFirmware()` (PSP ring `LOAD_IP_FW`), whose
measured results were:

| type | result |
|---|---|
| SMU (18) | `TEE_SUCCESS` — loaded |
| SDMA0/SDMA1 (9/10) | `0xFFFF0008` `TEE_ERROR_ITEM_NOT_FOUND` |
| RLC_G (8) | `0xFFFF0008` |
| CP ME/PFP/CE/MEC (1-4) | `0x80000203` |

There is **no fallback** — the loop logs a warning and continues.

### 10.4 What is already correct

Worth recording, because it was the first thing suspected:

- The load table already references `navi12_sdma.bin` / `navi12_sdma1.bin`
  (`amdbc250_dream_fw_load.c:169-170`).
- Those files exist in `firmware/`, are copied to `output/firmware/`, are
  installed to `C:\Windows\System32\drivers\bc-250\`, and are byte-distinct from
  the cyan blobs:

```
cyan_skillfish2_sdma.bin   15D0D3626DA7F2513F03B13BB7E02EEE
cyan_skillfish2_sdma1.bin  BD1C0B0F6A6A4F17EDE034F2C844B655
navi12_sdma.bin            6A60089CF0CFCC3E6248786AF5282AA8
navi12_sdma1.bin           9D7EE34E7ADA3BF47B3F35B5CB064E23
```

### 10.5 But SDMA firmware needs a different mechanism

There are **no SDMA IC_BASE definitions** in `inc/amdbc250_dream_hw.h` — only
`CP_PFP`, `CP_ME`, `CP_CE`. SDMA ucode is not loaded via CP IC_BASE. In amdgpu it
is bootstrapped by the SDMA engine itself through its own page-table mechanism
(`sdma_v3_0.c`). That has to be written before the fix can land.

### 10.6 `mmhub-vm-probe` hung the machine

Read-only by construction, and still hung the board. Do not re-run it as-is.

Two separate faults, both ours:
1. First run failed with `ERROR_INVALID_PARAMETER` (87) because the driver rejects
   autodetect (`MmioPhysicalBase=0`). Fixed by passing explicit BAR5 values —
   this is the form `gpu-init-explicit.exe` uses and it works.
2. Second run hung. Reading unvalidated MMIO addresses is **not** safe here.

---

## 11. Mesa / Vulkan on Windows — not started

- `F:\mesa-build` fails at configure: `-Dllvm=disabled -Damd-use-llvm=false` →
  `Feature llvm cannot be disabled: RadV ACO tests require LLVM`.
- No LLVM subproject is available.
- The source used was `%TEMP%\opencode\mesa-wdm` (temporary).
- `output\amdradv64.dll` (135 680 B) and `output\bc250_icd_stub.dll`
  (45 056 B, **7 exports**) are shims/stubs, not Mesa.
- `src/vulkan/bc250_vulkan_icd.c` is our own 110 KB stub, not an ACO consumer.

This is the only one of the three prerequisites that is a **build-configuration
problem** rather than a hardware or architectural one.

---

## 12. Where that leaves the project

Three independent prerequisites, none of which is sufficient alone:

| # | Prerequisite | State | Difficulty |
|---|---|---|---|
| 1 | Firmware actually loaded | **two real bugs found** (§10.1-10.3) | medium — SDMA bootstrap must be written |
| 2 | VMID + runlist-based TLB handling | not implemented; needs a KFD-equivalent scheduler | high |
| 3 | WDDM miniport + Mesa UMD | not started (§11) | high |

Ceiling for prerequisite 2: the validated workaround depends on KFD internals.
It is proven to work on Linux; nothing proves it cannot be made to work elsewhere,
but it is not a port, it is a reimplementation.

**Recommended order:** Mesa first (§11 — it is the only one that is a pure build
problem), then prerequisite 1, then decide whether a WDDM miniport is worth it.

### Corrections to earlier entries in this repository

| Old claim | Correction | Source |
|---|---|---|
| `SPI_PG` is SOS-locked on Windows | BIOS-version artifact, most likely — it is `0x1f` from VBIOS on P5.00 | `docs/16` |
| Write `RLC_PG_ALWAYS_ON_WGP_MASK` to keep WGPs on | no-op; already `0x1f` | `docs/16` |
| TMR setup is the next priority | TMR is inert `0xFF` even on working Linux | `tmz.bin` |
| `MC_VM_AGP_*` at `0x9528..` | wrong block; real MMHUB offsets are `0x6A0B8..0x6A0C0` | `mmhub_1_0_offset.h` |
| `aper_base` = BAR0 | APU uses `gfxhub->get_mc_fb_offset()` | `gmc_v10_0.c:698` |
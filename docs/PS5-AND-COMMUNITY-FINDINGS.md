# PS5 APU research — what applies to the BC-250, and what does not

The BC-250 is a cut-down PS5 Oberon die, so PS5 reverse engineering is the best
available source for two things this driver cannot learn on its own: what the
silicon can actually do, and what the firmware refuses to let a host do.

Everything below was gathered on 2026-09-30 from public repositories. It is
recorded here because it corrects two assumptions in the driver and because the
negative results save a lot of wasted hardware testing.

---

## 1. The GPU really does run at 2230 MHz

`aidenonlinux/PS5-Arch` turns a PS5 into a Linux desktop and states the part runs
at "8 CPU cores (16 threads) at 3.5 GHz and a GPU at 2.23 GHz". Its
`ps5_control` tool forces CPU 3500 MHz and GPU 2230 MHz, and adjusts the fan curve.

This is the same GPU as the BC-250's, so 2230 MHz is a demonstrated silicon limit
rather than a community guess.

**Applied:** the SMU whitelist's GPU ceiling was 2500 MHz, a number nothing has
ever reached. It is now 2230 MHz. Four shader arrays behind a mining cooler will
throttle or trip board protection long before the silicon gives up, and offering
a user 2500 MHz means letting them find out which happens the expensive way.

## 2. CPU VID above 1.325V risks bricking the board

`bc250_smu_oc`, the reference CPU overclocking tool, warns about this directly:
the CPU and GPU share one cooler on this chassis, and the author permanently
bricked a board by letting CPU VID exceed 1.325V.

**Applied:** the CPU boost ceiling was 5000 MHz with no attestation that it is
reachable. It is now 4000 MHz, the highest frequency reported stable with an
explicit VID setting.

## 3. Nobody implements `RequestActiveWgp` on this silicon

The 40 CU unlock writeup states it plainly:

> On Vangogh (another RDNA2 APU), the equivalent control is
> `SMU_MSG_RequestActiveWgp`. Cyan Skillfish doesn't expose that SMU message, but
> the SPI register is directly writable.

That matches what we measured: Q0 `0x18` is accepted (`0x01 OK`) and the active
count still stays `0`. It is a Van Gogh leftover with no implementation here.
The same source also records `RLC_PG_CNTL = 0`, meaning the harvested CUs are not
power gated in the first place — they were disabled by firmware policy.

**Consequence:** searching for a SMU power path to the WGPs is wasted effort on
this chip. The gate is the `SPI_PG` register and nothing else.

## 4. The unlock is two registers, and they work together

From `duggasco/bc250-40cu-unlock`, verified by a four-state A/B test on identical
hardware:

| State | pp512 tok/s | Power |
|---|---|---|
| Stock (CC=harvest, SPI=0x07) | 302 | 56W |
| SPI only | 302 | 140W |
| CC only | 302 | 55W |
| **Both** | **466** | 181W |

- `CC_GC_SHADER_ARRAY_CONFIG` is an **enumeration** input. It tells amdgpu, RADV
  and KFD how many CUs exist. Writing it changes what the driver reports and
  nothing else.
- `SPI_PG_ENABLE_STATIC_WGP_MASK` is the **dispatch gate**. It decides where the
  SPI actually sends wavefronts.

`filippor`'s independent `ignore_cu_harvest` parameter confirms the same point:
enumeration changes do not affect compute.

**Consequence:** a driver that reports 40 CUs while SPI still dispatches to 24
produces no speedup at all. Both writes are required, which is why our driver
already does both in the same step.

## 5. The harvest is a mask, not a defect — but not always

The original research boards had power, clocks and matching CGTS configuration on
the harvested CUs, which is why they unlock cleanly. That is **not universal**.
At least one board with the standard contiguous `■■■■■■□□□□` layout had two
genuinely defective unlocked WGPs:

- one hard-locked the GPU the instant it was routed
- one routed but returned ~5% wrong compute results

That board's maximum stable configuration was 36/40. Any tool that flips these
masks should say so rather than implying all 40 are usable.

## 6. What the PS5 jailbreak scene does and does not give us

Searched 2026-09-30: PS5Dev/PS5-UMTX-Jailbreak, ArkSama/PS5-Lapy-JB-Daemon,
etaHEN, onionHEN, mautz-kernel/ps5-hen, Collins3560/Cinebreak,
strongt1me/ps5-y2jb-autoloader, aidenonlinux/PS5-Arch.

**Useful**

- The 2230 MHz GPU figure and the working `ps5_control` boost path (section 1).
- A clear description of why the 8-core unlock works: the PS5 has all 8 cores and
  the mask hides two. That is the same shape as the CU harvest, which supports
  the idea that these are policy masks rather than silicon.
- `ps5-linux-loader` — already used here; see `inc/ps5_gpu_patterns.h`.

**Not applicable to Windows**

- **TMR relaxation / IOMMU patching.** `ps5-hen` patches the hypervisor to
  "patch IOMMU for unrestricted memory access". This is the capability that
  would let a guest reach registers a host driver cannot. Windows has no
  hypervisor layer to patch, and our driver is a normal kernel-mode client, so
  there is no equivalent to this. The security boundary that stops our `SPI_PG`
  write is not something a PS5 exploit removes.
- **VMCB patching, XOTEXT removal, SMAP/SMEP bypass** — all hypervisor-specific.
- **UMTX / Y2JB / Lapse / BD-JB chains** — these achieve kernel read/write on
  PS5 by attacking the console's own sandbox and hypervisor. They say nothing
  about a PC APU where we already run at ring 0.

**The honest summary:** the jailbreak scene confirms that this silicon *can* be
fully controlled — on a console that its own firmware trusts. It offers no
technique for a host OS, because it never needed one. It did produce the 2230 MHz
number and the eight-core-is-present framing, and those are what we used.

## 7. The PS5 jailbreak route is circular — there is nothing to copy

This was searched specifically for "which registers does a jailbroken PS5 touch,
and where does it access them from", because it looked like the most promising
remaining lead. It is not, and the reason is worth recording precisely.

### How the PS5 GPU exploit actually works

`mia/ps5-linux-loader` (`include/gpu.h`, `source/gpu.c`) is the clearest example.
It does not touch a single GC power register. It:

```c
s_gpu.fd = open("/dev/gc", O_RDWR);
gpu_walk_pt(vmid, rel_va, &page_size);          // walk GPU page tables
kernel_setlong(s_gpu.victim_ptbe_va, new_ptbe);  // repoint one PTE
gpu_submit_commands(s_gpu.fd, 0, 1, desc_va);    // PM4 DMA via /dev/gc
```

which yields `gpu_read_phys8()` / `gpu_write_phys8()` — **arbitrary physical
memory read and write, including kernel memory.**

### Why it does not transfer

That capability requires four things. Three of them are exactly what this project
does not have:

| Requirement | Status |
|---|---|
| `/dev/gc` accepting PM4 submissions | We have the equivalent |
| **Working WGPs so the DMA engine executes** | **This is the thing being sought** |
| **Working GPU page tables / GART** | Blocked by the 0x1A class of crashes |
| **Rewriting a PTE** | Needs kernel privilege |

They can DMA arbitrary memory *because their GPU already runs*. We would need it
to do it. The dependency runs in a circle.

### The deeper reason: the PS5 has nothing to unlock

Sony shipped the PS5 with all 36 CUs enabled. There is no harvest mask on the
console to bypass, so no PS5 tool exists for lifting one. The BC-250's 24-of-40
is a mining-board configuration choice — the CU mask was *turned off*, not locked
by the vendor.

This reframes the whole search: there is no "PS5 method" that could be ported,
because nobody ever needed one. Sony's firmware already had the units enabled.

The 8-core case is the same shape, and it is the honest parallel to our WGP
effort: the PS5 has all 8 cores, a mask hides two, and lifting the mask is a
single SMU write. Our board does have all 8 cores too, which is why
`SMU[0x0115A870]` unlocks it. Both are policy masks over present silicon. The
difference is that the CPU mask is reachable through the SMU, and the CU mask is
not.

### What the search did confirm about our own work

The PS5 loader's DMA descriptor flags are bit-identical to what this driver
independently derived:

```c
dma_hdr = (1u << 31)   /* cp_sync          */
        | (2u << 25)   /* dst_cache_policy */
        | (1u << 27)   /* dst_volatile     */
        | (2u << 13)   /* src_cache_policy */
        | (1u << 15);  /* src_volatile     */
```

Same header (`pm4_type3_header(DMA_DATA, 6)`) and same 16-byte command descriptor.
See `inc/ps5_gpu_patterns.h` and `dma-move-verify`, which confirmed real byte
movement on this board. The PM4 work is validated against a known-good
implementation.

### Summary

Searching for "how a jailbroken PS5 unlocks things" returns nothing applicable,
and now we know why rather than merely having failed to find it. The technique
requires a working GPU; the goal is to make the GPU work; therefore the technique
cannot be the answer. This is a closed question, not an unexplored one.

---

| Project | What it gave us |
|---|---|
| `aidenonlinux/PS5-Arch` | 2230 MHz GPU, 3.5 GHz CPU, `ps5_control` boost |
| `duggasco/bc250-40cu-unlock` | Two-register A/B test, harvest maps, defective-WGP field report |
| `filippor/cyan-skillfish-governor` (smu branch) | `ignore_cu_harvest` confirmation |
| `bc250-collective/bc250_smu_oc` | 1.325V CPU VID brick warning |
| `rw-r-r-0644/bc250-core-unlock` | Core mask `0x0115A870`, Q3 `0x98` primitive |
| `movacx/bc250-control-center` | Verified BIOS flashing kit; community limit table |
| `elektricm/amd-bc250-docs` | Stock P3.00 and Chipset Menu images |
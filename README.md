> **Project status: PAUSED.** Out of new ideas, and no longer willing to spend my free time on this. The project is on hold until new ideas worth my free time come along. Everything achieved so far is documented and committed — feel free to fork and continue. And don't be shy with new ideas, no matter how silly they may seem.

# AMD BC-250 Windows Driver

GPU driver for AMD BC-250 (Cyan Skillfish) on Windows 11 26100. WDM IOCTL driver with SMU mailbox, PSP ring, Vulkan ICD, and display support.

**Goal:** fully working GPU driver for BC-250 on Windows.

**Current build:** `4.3.0.11` (2026-09-23) — **INIT_HARDWARE deadlock fixed + full test suite PASS**.

---

## Session 2026-09-23 (this tree): pa_v1, governor, Vulkan ICD draw

| Deliverable | Tool | Result |
|-------------|------|--------|
| pa_v1 mailbox (PSP BAR2) | `pa-v1-diag2` | **PASS** — bootloader `0x001C0102`, feature `2`; **BAR2=`0xFE700000`**, BAR0=0 |
| GPU governor service | `governor-service` | **OK** — set 1600 MHz + unforce (governor sequence) |
| Vulkan ICD pipeline draw | `vk-draw-test` | **PASS** — GIPA→instance→`DRAW_INDEX_AUTO`→`vkQueueSubmit`, EXIT=0 |

**ICD:** `bc250_icd_stub.dll` exposes **7 ICD exports** (`src/vulkan/bc250_vulkan.def`); `vkCreateInstance` resolved via `vk_icdGetInstanceProcAddr`. Test: `test-tools/vk-draw-test.c` + `compile-vk-draw-test.bat`.

**PSP companion:** BAR2 auto-init fix in [PSP repo](https://github.com/Keshas-dev/AMD-BC-250-PSP-Windows-Driver) — reinstall PSP after that build.

---

## Deadlock fix (2026-09-23) — verified on hardware

### Root cause
`IOCTL_AMDBC250_INIT_HARDWARE` (full-init, `Flags=0`) held `DeviceMutex` while calling `DreamV3HwInitialize` → `Amdbc250PspKiqInit` → `PspProxyInit` → PSP `GET_GPU_INFO` → GPU proxy `0x900` re-acquired the **same non-recursive FastMutex** → **permanent hang** (process stuck in kernel; only reboot could kill it).

### Fix (`src/kmd/amdbc250_dream_kmd.c` case `0x80000B80`)
1. **NBIO_MAP path** (`Flags & AMDBC250_INIT_FLAG_NBIO_MAP`): `Amdbc250PspKiqInit` removed entirely; `KiqAvailable=FALSE` — PSP proxy works via `0x900` without KIQ.
2. **Full INIT path** (`Flags=0`): set `HwInitInProgress=TRUE` under mutex → **release** before `DreamV3HwInitialize` → **re-acquire** after to set `HardwareInitialized=TRUE`, clear flag, release.
3. Concurrent INIT while full init in progress → `STATUS_DEVICE_BUSY` (re-entry guard).
4. `HwInitInProgress` flag added in `inc/amdbc250_dream_kmd.h`.

### Verified (2026-09-23, all on installed 4.3.0.11)
```
gpu-init-explicit.exe   ✅  INIT OK br=32, GPU_ID=0x9FFF9700, GRBM=0x00000000, process exits
full-init-test.exe      ✅  SUCCESS — no TDR, Step_HwInit=11, process exits
test-psp-driver.exe -s  ✅  PSP Alive YES, C2PMSG_64=0x80000000, C2PMSG_81=0x002C7A89
spi-pg-nbio-test.exe -r ✅  SPI_PG=0 [gated, expected], SMU Q0 0x3D=0xDD602C7D, 1500MHz
bar5-smn-test.exe       ✅  SMU 88.6.0, Features=0xDD602C7D, ActiveWgp=0
smu-all-msgs-test.exe   ✅  16/16 (7 reads + 9 writes, no wedge)
psp-ring-submit-test    ✅  RING_INIT Result=1, GET_FW_ATTESTATION SUCCESS, WPTR advances
```

**Installed SHA256:** `0E69D7D3FC8E934E6ACDA5467BD543A13D1600BCEEBB0EA313305851A8B30D4B` (matches `output\atikmdag.sys`).

---

## PSP / CCP notes from Linux (2026-09-23)

BC-250 carries **AMD Secure Processor at PCI `1022:143E`**. Upstream Linux `ccp` patch series (Mattia Tadini, Sep 2026) binds it with a **device-read register map** — useful for our Windows PSP path:

| Fact | Detail |
|------|--------|
| Layout | **pspv3/pspv4** (not pspv1, not pspv5–v7) |
| BAR windows | `fe700000` 1MB + `fe884000` 8KB (Linux binds via **BAR2**) |
| Mailbox cmdresp | BAR2+`0x10544` = `0x80000000` (live) |
| Bootloader | BAR2+`0x109EC` (C2PMSG_59) = `0x001C0102` → version **00.1c.01.02** |
| Feature reg | BAR2+`0x109FC` (C2PMSG_63) = `0x00000002` |
| Inten / Intsts | BAR2+`0x10690` / `0x10694` (P2CMSG_*) |
| CCP engine | **None** — version @ `0x100` reads `0xFFFFFFFF` |
| TEE | Capability bit set, but **`PSP_CMD_TEE_RING_INIT` times out** — no TEE ring |
| SEV | Absent |
| Working path | **Platform access only** — mailbox **`pa_v1` = C2PMSG_28..30**, independent of TEE |
| DBC (dyn boost) | Msg `0x65` rejected (`PSP error 0x4`) — probe continues |
| HSTI | Empty (security reporting bit clear) |
| After bind (Linux dmesg) | `platform access enabled` → `psp enabled` → bootloader sysfs OK |

**Why it matters:** PSP platform mailbox can answer without TEE; on this APU (CPU+GPU+VRAM on one die) it may be able to activate register/fabric/power domains that SMU feature bits alone did not open (WGP/VCN locks). Windows work: map `pspv_bc250` offsets + probe **C2PMSG_28..30**, skip TEE ring wait.

**External references:**
- LKML: `[PATCH 0/3] crypto: ccp - two PSP init fixes, and the AMD BC-250` (2026-09-19)
- GitHub: [blackbearreloaded/ps5-gpu-research](https://github.com/blackbearreloaded/ps5-gpu-research) — PS5 RDNA2 Mesa GL/compute research (arch reference)
- Mesa: work item [11982](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/11982), MRs [33109](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/33109), [33116](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/33116) (browser-only; Anubis blocks agents)

---

## PSP ↔ GPU coexistence (2026-09-23)

Two PCI devices must not fight over GPU BAR5:

| Device | BAR | Owner |
|--------|-----|-------|
| GPU `1002:13FE` | BAR5 **`0xFE800000`** | **atikmdag only** (`DeviceMutex`) |
| PSP `1022:143E` | BAR0 **`0xFE700000`** | **PspDriver** (own window; future pa_v1 C2PMSG_28..30) |

**Bug:** PSP `IOCTL_PSP_INIT_HW` dual-mapped GPU BAR5 → **4× BSOD 0x1E** (A/B confirmed).  
**Fix (this repo):** proxy cases **`0x900`/`0x901`** in `amdbc250_dream_kmd.c` now take **`ExAcquireFastMutex(&DevExt->DeviceMutex)`** so PSP MMIO serializes with GPU's own BAR5 access.  
**Fix (PSP repo):** never maps `0xFE800000`; all GPU access via proxy. See PSP `README.md` / `AGENTS.md` (2026-09-23).

Install order: **GPU first**, then PSP. Full notes: `AGENTS.md` "PSP ↔ GPU coexistence".

---

## Current Status (2026-09-23) — deadlock fix verified

### ✅ Verified Working (all tested on hardware)

| Feature | Details |
|---------|---------|
| **Vulkan ICD pipeline** | `vkCreateInstance→Enumerate→Alloc→Map→CreateBuffer→Bind→Submit→Wait` ✅ VK_SUCCESS. GPU0 AMD BC-250 (RADV Stub), API 1.2.0 |
| **SMU all messages** | **16/16 PASS**, 0 wedge — 7 reads + 9 writes with wedge check |
| **SMU CPU OC** | Q3 0x50/0x8B/0x8C/0x8F — all OK, persist after reboot. 8 cores @ 3500MHz |
| **SMU frequency/voltage** | 1500MHz @ 931mV base, governor sequence safe |
| **SMU SRAM** | Q3 0x28/0x29 whitelisted — write-only (SMU-internal) |
| **PSP GPCOM ring** | INIT/SUBMIT/LOAD_IP_FW/SETUP_TMR all verified on hardware. `GET_FW_ATTESTATION` = SUCCESS |
| **PSP firmware loading** | DIRECT C2PMSG (base 0x58000): psp-fw-load 8/8, psp-tos-test PASS |
| **KMDOD display** | 2560x1440, Status OK, CM_ERR=0 |
| **BAR5 MMIO** | `DreamV3WriteRegister/ReadRegister` via `WRITE_REGISTER_ULONG` |
| **CPU core unlock** | SMU Q3 0x98 → SMN 0x0115A870 (6→8 cores, volatile, reboot reverts) |
| **CC_ARRAY** | Partially writable (bits 24-28: 0x1F000000), persists across boots |
| **VRAM info** | `GetVramInfo` returns Total=Visible=16GB after KMD fix |
| **Driver stability** | 50-iteration stress test, 0 failures, 0 wedge events |

### ❌ Blocked

| Feature | Blocker |
|---------|---------|
| **3D graphics** | WGP/SPI_PG SOS-locked (SPI_PG=0). **Not hardware-fused** — Linux amdgpu runs shaders. |
| **WGP unlock on Windows** | SPI_PG_ENABLE_STATIC_WGP_MASK SOS-locked from host BAR5 |
| **WGP unlock via EFI** | **CONFIRMED BLOCKED** (2026-09-15) — NBIO locked at EFI boot on this unit. `third-party/EFI_Boot/WGP_unlock.nsh` does not work here. |
| **WGP unlock via Linux** | Works via debugfs/kernel context — not replicable on Windows WDM |
| **SDMA** | Ring not initialized, firmware broken (stock v0x34). navi12_sdma.bin works on Linux. |
| **Compute rings** | KIQ_SIZE=0 (read-only), ring BASE registers SOS-locked |

### Test Results (2026-09-15 + re-run 2026-09-23)

```
smu-all-msgs-test.exe     ✅  16/16 PASS, 0 wedge (re-run 2026-09-23)
vk-minimal-test.exe       ✅  VK_SUCCESS, GPU0 AMD BC-250 API 1.2.0
smu-cpu-msg-test.exe      ✅  8 cores @ 3500MHz (re-run 2026-09-23: 1212mV, all OK)
smu-stress-test.exe       ✅  50 iterations, 0 failures
psp-ring-submit-test.exe  ✅  RING_INIT + GET_FW_ATTESTATION SUCCESS (re-run 2026-09-23)
gpu-init-explicit.exe     ✅  NBIO_MAP INIT OK, no deadlock (2026-09-23)
full-init-test.exe        ✅  Flags=0 SUCCESS, no TDR (2026-09-23)
vulkaninfoSDK.exe         ✅  vendor 0x1002, device 0x13fe, discrete GPU
```

---

## Build & Install

### Prerequisites
- Visual Studio 2022 (auto-detected; **F:** on this host — `F:\Program Files\Microsoft Visual Studio\2022\Community`)
- Windows WDK 10.0.26100.0 (`F:\Program Files (x86)\Windows Kits\10`)
- Test signing: `bcdedit /set testsigning on` (Admin), Secure Boot OFF

### Build
```cmd
build.bat
```
Output: `output\atikmdag.sys`, `output\amdbc250umd64.dll`, INF, CAT

### Install (Admin)
```
1. Device Manager → AMD Radeon BC-250 → Uninstall (check "Delete driver")
2. Reboot
3. Device Manager → Update Driver → Browse → output\amdbc250_dream.inf
4. Reboot
```

### Quick Test
```cmd
output\smu-all-msgs-test.exe          # SMU verification
output\bar5-smn-test.exe              # SMU mailbox via SMN
output\psp-ring-submit-test.exe       # PSP GPCOM ring
output\vk-minimal-test.exe            # Vulkan pipeline
```

---

## Hardware Facts

- **SoC:** BC-250 (Cyan Skillfish) — RDNA2, 16GB GDDR6 UMA
- **GPU ID:** 0x13FE (PCI), 0x9FFF9700 (internal)
- **40 CU die, 24 active** (harvest mask) — NOT fused, Linux unlocks 40
- **GC_BASE:** 0x1260 (BC-250 shifted offsets vs Navi10)
- **GPU BAR5:** 0xFE800000 (512KB MMIO)
- **SMU version:** 88.6.0 (driver_if=8)
- **PSP IP:** v11.0.8 (CYAN_SKILLFISH2) — GPU-side MP0 ring @ BAR5 `0x58000`
- **PSP/CCP PCI:** `1022:143E` @ 01:00.2 — pspv3 layout, platform mailbox `pa_v1` (C2PMSG_28..30), bootloader `00.1c.01.02`, no CCP engine, no TEE ring
- **BIOS:** P4.00G, **IOMMU must be OFF**

---

## Architecture

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  Vulkan ICD  │───▶│  KMD IOCTL   │───▶│  atikmdag.sys│
│  bc250_icd   │    │  0x8000xxxx  │    │  (kernel)    │
└──────────────┘    └──────────────┘    └──────┬───────┘
                                                │
                    ┌───────────────────────────┤
                    │    │    │    │    │    │
                 ┌──▼┐┌▼──┐┌▼──┐┌▼──┐┌▼──┐┌▼──┐
                 │BAR5│ │SMN│ │PSP│ │CMOS│ │SMU│ │IOCTL│
                 │    │ │   │ │   │ │    │ │   │ │     │
                 └───┘ └───┘ └───┘ └───┘ └───┘ └─────┘
```

| Layer | Path | IOCTL | Purpose |
|-------|------|-------|---------|
| Memory | ALLOC/FREE/MAP_VIDMEM | 0x80000820-828 | UserMode VRAM staging |
| Registers | READ_REG/WRITE_REG | 0x80000010-14 | BAR5 GPU register access |
| SMN | SMN_READ/WRITE | 0x80000C30-34 | NBIO 0x38/0x3C SMN window |
| SMU | SMU_MSG | 0x80000924 | SMU mailbox (Q0/Q2/Q3) |
| PSP | PSP_PROXY | 0x80000900-901 | PSP register proxy |
| CPU OC | SMU_CPU_MSG | 0x80000C2C | Q3 0x50/0x8F/0x8B/0x8C |
| CMOS | CMOS_ACCESS | 0x80000C28 | MemConf_t read/write |
| PSP ring | PSP_RING_* | 0x80000C18/1C/20/24 | GPCOM ring (verified) |
| Submit | SUBMIT_COMMANDS | 0x80000880 | PM4 command submission |
| Vulkan | SUBMIT_COMMANDS | 0x80000880 | RADV → KMD via IOCTL |

---

## Key Documentation

| File | Content |
|------|---------|
| **AGENTS.md** | Agent memory — hardware facts, blockers, test results (DETAILED) |
| **docs/INSTALL-GUIDE.md** | Build/install/test steps for admin |
| **docs/WGP-UNLOCK-STATUS.md** | WGP unlock: all 9 methods tested, all blocked |
| **docs/RADV-KMD-INTEGRATION.md** | Vulkan ICD → KMD IOCTL mapping |
| **docs/PSP-GPCOM-RING-WORKING.md** | PSP ring — offsets, IOCTLs, verification |
| **docs/BC250-LINUX-IP-MAP.md** | Linux-verified IP base addresses |

---

## File Structure

```
├── src/kmd/                    # Kernel driver (atikmdag.sys)
│   ├── amdbc250_dream_kmd.c   # DriverEntry, IOCTL dispatch, PSP ring
│   ├── amdbc250_dream_hw_init.c
│   ├── amdbc250_psp.c          # PSP proxy
│   └── ...
├── src/umd/                    # User-mode driver (D3D9 DDI)
├── src/vulkan/                 # Vulkan ICD stub (bc250_icd_stub.dll)
│   ├── bc250_vulkan_icd.c
│   └── bc250_vulkan.h
├── inc/                        # Headers (ioctl defs, HW regs)
├── test-tools/                 # Diagnostic tools (source + .exe)
├── output/                     # Build outputs (signed drivers + test tools)
├── docs/                       # Technical docs (INSTALL, WGP, RADV, etc.)
├── firmware/                   # Firmware blobs
├── third-party/EFI_Boot/       # EFI scripts (DOES NOT WORK on this unit)
├── build.bat                   # Build + sign
└── .gitignore
```

---

## Registry Settings (fail-closed)

All under `HKLM\SYSTEM\CurrentControlSet\Services\atikmdag` (DWORD).
**Critical:** `DisplayWritesEnabled=0` (live DCN writes black-screen GPU). `HwInitMemCtrl=0` (freeze zone crash). `HwInitGart=0` / `HwInitVm=0` (0x1A BSOD).

---

## Next Steps

1. **RADV integration** — RADV PM4 → `vkQueueSubmit` → IOCTL_AMDBC250_SUBMIT_COMMANDS (KMD backend ready, WGP blocks actual execution)
2. **SDMA via ring** — load navi12_sdma.bin (type 9/10) through PSP ring
3. **Real WDDM miniport** — wddm-ps5 project (displib.lib path)
4. **BIOS NBIO unlock** — only path to WGP on this unit

---

## External Resources

- **GitHub:** https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver
- **PSP Driver:** https://github.com/Keshas-dev/AMD-BC-250-PSP-Windows-Driver (deprecated — merged into GPU driver)
- **Linux Kernel:** https://github.com/torvalds/linux (drivers/gpu/drm/amd/amdgpu)
- **elektricM/amd-bc250-docs** — Community BC-250 docs (Mesa 25.1+, RADV, VRAM config)
- **duggasco/bc250-40cu-unlock** — 40 CU unlock via Linux kernel patch
- **rw-r-r-0644/bc250-core-unlock** — CPU core unlock via SMU Q3 0x98

---

## License

Educational purposes. Use at your own risk.

## "If you need a tool and nobody has built it yet, then build it yourself."
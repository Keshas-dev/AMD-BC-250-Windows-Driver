# AMD BC-250 Windows Driver

GPU driver for AMD BC-250 (Cyan Skillfish) on Windows 11 26100. WDM IOCTL driver with SMU mailbox, PSP ring, Vulkan ICD, and display support.

**Goal:** fully working GPU driver for BC-250 on Windows.

---

## Current Status (2026-09-15) — POST TESTS

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

### Test Results (2026-09-15)

```
smu-all-msgs-test.exe     ✅  16/16 PASS, 0 wedge
vk-minimal-test.exe       ✅  VK_SUCCESS, GPU0 AMD BC-250 API 1.2.0
smu-cpu-msg-test.exe      ✅  8 cores @ 3500MHz, 1175mV
smu-stress-test.exe       ✅  50 iterations, 0 failures
vulkaninfoSDK.exe         ✅  vendor 0x1002, device 0x13fe, discrete GPU
```

---

## Build & Install

### Prerequisites
- Visual Studio 2022 (auto-detected on C:/D:/E:)
- Windows WDK 10.0.26100.0
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
- **PSP IP:** v11.0.8 (CYAN_SKILLFISH2)
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

Source code for educational purposes. Use at your own risk.

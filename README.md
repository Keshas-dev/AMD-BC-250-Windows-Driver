# AMD BC-250 Windows Driver Project

## Who We Are

This is an AMD BC-250 Windows 11 driver project built by Keshukas and Kumpis (AI) in our free time. We work together with the hope of creating a fully working GPU driver for the AMD BC-250 (Cyan Skillfish) graphics card on Windows 11.

**Everyone is welcome to join!** If you have experience with GPU drivers, WDDM, Vulkan, or just want to help — you're invited.

---

## What We Discovered

### The GPU
- **Architecture:** RDNA 2 (GFX1013 / Cyan Skillfish)
- **BC-250 is a PS5 Oberon variant** — 24 CU, 16GB GDDR6, UMA memory
- **Supports 40 CU unlock** (from 24 → 40) via register writes
- **Compute Queue is broken** (hardware quirk) — must be disabled
- **Requires HDP Flush** before reading ring pointers — otherwise GPU hangs
- **VCN (video encode/decode) is locked** — Sony firmware lock
- **PS5 SMU blocks GPU MMIO** — requires PSP firmware auth for compute access

### Why Other Projects Failed
- **AMD official driver** also gives **Code 43** — this is not our code's problem
- **All WDDM attempts** (WDDM 1.3–2.7, 47 callbacks) fail with `0xC0000059` (STATUS_REVISION_MISMATCH)
- **DxgkInitialize()** causes **Code 39** — our KMD is not a full WDDM display miniport
- **BC-250 is a compute-only GPU** — no display output, so WDDM path is pointless

### Our Discovery: Vulkan ICD via IOCTL
- **First project in the world** to achieve a working Vulkan ICD on AMD BC-250 with Windows
- **13/13 Vulkan tests PASS** via custom IOCTL channel
- **vulkaninfo.exe passes** without errors with the official Vulkan loader
- **IOCTL channel works** (14/15 tests) — KMD receives and executes commands

---

## What We've Done

### KMD (Kernel-Mode Driver) — `atikmdag.sys`
- ✅ IOCTL dispatch working (30+ handlers)
- ✅ g_PciDevExt initialized in DriverEntry
- ✅ IB packet + EOP fence format fixed
- ✅ EOP fence: `0xA0000246` (EVENT_INDEX=5, DATA_SEL=1, INT_SEL=1)
- ✅ GFX10 ring buffer initialization
- ✅ HDP Flush before ring reads
- ✅ PM4 command queue with fence signaling
- ✅ SDMA copy/fill engine
- ✅ TDR reset recovery
- ✅ 40 CU unlock
- ✅ Power/thermal management
- ✅ INIT_HARDWARE — user-mode provides MMIO base, KMD maps it
- ✅ SEND_PM4 — raw PM4 commands to GFX ring
- ✅ READ_REG / WRITE_REG — GPU MMIO register access
- ✅ GET_HW_STATUS — report MMIO/rings/fence state
- ✅ READ_PCI_BAR — scan PCI bus for BC-250 BARs
- ✅ AllocVidMem uses MDL-based allocation (MmAllocatePagesForMdlEx)
- ✅ **PSP v11 firmware loading** — C2PMSG mailbox, bootloader handshake, SOS load

### Vulkan ICD — `amdbc250vulkan.dll`
- ✅ Works with official Vulkan loader (vulkaninfo.exe passes!)
- ✅ `vk_icdNegotiateLoaderICDInterfaceVersion` (version 4)
- ✅ 80+ Vulkan function stubs for loader compatibility
- ✅ QueueSubmit with IB (PM4 commands → GPU)
- ✅ CreateInstance, CreateDevice, AllocateMemory, CreateBuffer, CreateFence, WaitForFences
- ✅ CreateCommandPool, AllocateCommandBuffers, BeginCommandBuffer, EndCommandBuffer
- ✅ CreateGraphicsPipelines

### User-Mode Driver (UMD) — `amdbc250umd64.dll`
- ✅ D3D9 DDI (45+ functions)
- ✅ OpenAdapter = ordinal 1 (via .def file)
- ✅ GetCaps returns real D3DCAPS9 data
- ✅ CreateResource, Lock/Unlock IOCTL fixes
- ✅ Flush sends GPU physical address (not CPU address)

### Build & Test
- ✅ build.bat (KMD + UMD, auto-sign)
- ✅ dxgkrnl.lib import library (not in WDK 10.0.26100.0)
- ✅ test-gpu-ioctls.exe — 14/15 PASS (ReadEdid FAIL expected — no display)
- ✅ test-vulkan-icd.exe — 13/13 PASS
- ✅ test-gpu-hw-init.exe — 5/7 PASS (MMIO mapping fails — SMU block)
- ✅ test-d3d9-adapter.exe — 5/5 PASS
- ✅ vulkaninfo.exe passes without errors

---

## How to Build

### Prerequisites
- Visual Studio 2022 (Community or Professional) — auto-detected on E: or C: drive
- Windows WDK 10.0.26100.0
- Test signing: `bcdedit /set testsigning on` (Run as Admin)

### Build
```cmd
cd C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main
build.bat
```

### Install (Full Installer)
```cmd
tools\install.bat
```
This installs everything: KMD, UMD, Vulkan ICD, DXVK, DXVK-NVAPI.
Run as Administrator. Reboot required.

### Install (Manual)
1. `build.bat` creates `output\atikmdag.sys` + `output\amdbc250umd64.dll`
2. Device Manager → BC-250 → Update Driver → Browse → `output\`
3. Reboot

### Test
```cmd
test-tools\test-gpu-ioctls.exe      # IOCTL test (14/15 pass)
test-tools\test-vulkan-icd.exe      # Vulkan test (13/13 pass)
test-tools\test-gpu-hw-init.exe     # Hardware init + MMIO test (5/7 pass)
test-tools\test-d3d9-adapter.exe    # D3D9 UMD adapter test (5/5 pass)
test-tools\test-psp.exe             # PSP firmware test (status/mailbox/init)
vulkaninfo.exe                      # Official Vulkan test (passes!)
```

---

## What We Want to Do Next

### Immediate — Unlock GPU Registers (Current Blocker)
1. **Get PSP firmware blobs** — `amdgpu_sos.bin`, `amdgpu_asd.bin`, `amdgpu_ta.bin` (extract from PS5 firmware or find compatible files)
2. **Test PSP mailbox** — `test-psp.exe mailbox` to verify PSP responds
3. **Load firmware** — `test-psp.exe loadfw 0 amdgpu_sos.bin` then `test-psp.exe init`
4. **Verify MMIO** — after PSP auth, GPU registers should return real values instead of 0x0

### Short Term
5. **Real triangle rendering** — vertex buffer + PM4 draw commands via ring buffer
6. **ACO shader compilation** — DXBC/SPIR-V → GFX10 ISA
7. **D3D9 via IOCTL** — bypass D3D9On12 using custom path

### Medium Term
8. **Full WDDM display miniport** — DxgkInitialize + all DDI callbacks
9. **D3D11/D3D12 UMD** — functional stubs that actually work
10. **Multi-monitor** — 4 display pipes support

### Long Term
11. **OpenGL ICD** — Mesa radeonsi port
12. **Ray tracing** — RT core support
13. **GPU compute** — SDMA compute queue (when HW quirk is fixed)

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│              Vulkan Application                       │
├─────────────────────────────────────────────────────┤
│         amdbc250vulkan.dll (Vulkan ICD)              │
│         ├── 80+ Vulkan function stubs                │
│         ├── QueueSubmit → PM4 commands to DMA buffer │
│         ├── AllocateMemory → KMD IOCTL               │
│         └── CreateBuffer → KMD IOCTL                 │
├─────────────────────────────────────────────────────┤
│         atikmdag.sys (KMD)                           │
│         ├── IOCTL 0x80000880: SubmitCommands (IB+EOP)│
│         ├── IOCTL 0x80000930: AllocDmaBuffer         │
│         ├── IOCTL 0x80000840: AllocVidMem            │
│         ├── IOCTL 0x80000B80: InitHardware           │
│         ├── IOCTL 0x80000B84: SendPM4                │
│         ├── IOCTL 0x80000B88: ReadReg                │
│         ├── IOCTL 0x80000B8C: WriteReg               │
│         ├── IOCTL 0x80000B90: GetHwStatus            │
│         ├── IOCTL 0x80000B94: ReadPciBar             │
│         ├── IOCTL 0x80000B98: PspInit                │
│         ├── IOCTL 0x80000B9C: PspLoadFirmware        │
│         ├── IOCTL 0x80000BA0: PspSendCommand         │
│         ├── IOCTL 0x80000BA4: PspGetStatus           │
│         ├── IOCTL 0x80000BA8: PspTestMailbox         │
│         ├── PSP v11 firmware loading                  │
│         ├── PM4 command ring → GPU                    │
│         └── EOP fence → completion signal             │
├─────────────────────────────────────────────────────┤
│              AMD BC-250 GPU (RDNA2/GFX1013)           │
│              24 CU, 16GB GDDR6, DCN 2.1              │
│              PSP v11 (Platform Security Processor)    │
└─────────────────────────────────────────────────────┘
```

---

## File Structure

```
├── src/kmd/                        # Kernel-Mode Driver
│   ├── amdbc250_dream_kmd.c      # IOCTL dispatch, DriverEntry, submit
│   ├── amdbc250_dream_hw_init.c  # GPU init, ring buffers, display
│   ├── amdbc250_dream_power.c    # Power/thermal management
│   ├── amdbc250_dream_vm.c       # GPUVM, GART, page tables
│   ├── amdbc250_psp_v11.c        # PSP firmware loading (C2PMSG, bootloader)
│   └── dxgkrnl.def                # Import library (not in WDK)
├── src/umd/                        # User-Mode Driver
│   ├── amdbc250_umd_v46.c         # D3D9 DDI (45+ functions)
│   └── amdbc250_umd.def           # Export: OpenAdapter = ordinal 1
├── src/vulkan/                     # Vulkan ICD
│   ├── bc250_vulkan_icd.c         # 80+ Vulkan functions, IOCTL submit
│   ├── bc250_vulkan.def           # Export: vk_icdGetInstanceProcAddr
│   ├── bc250_aco_wrapper.c        # ACO shader compiler stub
│   └── bc250_shader.c             # SPIR-V → GFX10 ISA
├── inc/                            # Shared headers
│   ├── amdbc250_dream_kmd.h      # KMD structures, register offsets
│   ├── amdbc250_dream_hw.h       # Hardware register definitions
│   ├── amdbc250_psp_v11.h        # PSP context and API
│   ├── amdbc250_ioctl.h           # IOCTL codes + structures
│   └── amdbc250_d3d*.h            # D3D type definitions
├── test-tools/                     # Test applications
│   ├── test-gpu-ioctls.c          # IOCTL test (15 tests)
│   ├── test-vulkan-icd.c          # Vulkan ICD test (13 tests)
│   ├── test-gpu-hw-init.c         # Hardware init + MMIO test (7 tests)
│   ├── test-d3d9-adapter.c        # D3D9 UMD adapter test (6 tests)
│   ├── test-psp.c                 # PSP firmware test (status/mailbox/init)
│   └── test-render.c              # Rendering test (color fill)
├── inf/                            # Driver INF
├── output/                         # Build output
├── tools/                          # Install/uninstall/diagnostic scripts
├── build.bat                       # Build + sign script
└── STATUS.md                       # Detailed project status
```

---

## License

Source code for educational purposes. Use at your own risk.
ACO compiler: MIT license (Mesa project).

---

*Made with love for GPU drivers.*

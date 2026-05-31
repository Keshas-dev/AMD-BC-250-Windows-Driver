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

### Why Other Projects Failed
- **AMD official driver** also gives **Code 43** — this is not our code's problem
- **All WDDM attempts** (WDDM 1.3–2.7, 47 callbacks) fail with `0xC0000059` (STATUS_REVISION_MISMATCH)
- **DxgkInitialize()** causes **Code 39** — our KMD is not a full WDDM display miniport
- **BC-250 is a compute-only GPU** — no display output, so WDDM path is pointless

### Our Discovery: Vulkan ICD via IOCTL
- **First project in the world** to achieve a working Vulkan ICD on AMD BC-250 with Windows
- **13/13 Vulkan tests PASS** via custom IOCTL channel
- **vulkaninfo.exe passes** without errors with the official Vulkan loader
- **IOCTL channel works** (13/15 tests) — KMD receives and executes commands

---

## What We've Done

### KMD (Kernel-Mode Driver)
- ✅ IOCTL dispatch working (24+ handlers)
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

### Vulkan ICD
- ✅ Works with official Vulkan loader (vulkaninfo.exe passes!)
- ✅ `vk_icdNegotiateLoaderICDInterfaceVersion` (version 4)
- ✅ 80+ Vulkan function stubs for loader compatibility
- ✅ QueueSubmit with IB (PM4 commands → GPU)
- ✅ CreateInstance, CreateDevice, AllocateMemory, CreateBuffer, CreateFence, WaitForFences
- ✅ CreateCommandPool, AllocateCommandBuffers, BeginCommandBuffer, EndCommandBuffer
- ✅ CreateGraphicsPipelines

### User-Mode Driver (UMD)
- ✅ D3D9 DDI (45+ functions)
- ✅ OpenAdapter = ordinal 1 (via .def file)
- ✅ GetCaps returns real D3DCAPS9 data
- ✅ CreateResource, Lock/Unlock IOCTL fixes
- ✅ Flush sends GPU physical address (not CPU address)

### Build & Test
- ✅ build.bat (KMD + UMD)
- ✅ dxgkrnl.lib import library (not in WDK 10.0.26100.0)
- ✅ test-vulkan-icd.exe (13 tests, all pass)
- ✅ test-gpu-ioctls.exe (15 tests, 13 pass)
- ✅ vulkaninfo.exe passes without errors

---

## How to Build

### Prerequisites
- Visual Studio 2022 (Community or Professional)
- Windows WDK 10.0.26100.0
- Test signing: `bcdedit /set testsigning on` (Run as Admin)

### Build
```cmd
cd C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main
build.bat
```

### Install
1. `build.bat` creates `output\atikmdag.sys` + `output\amdbc250umd64.dll`
2. Device Manager → BC-250 → Update Driver → Browse → `output\`
3. Reboot

### Register Vulkan ICD
```cmd
reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "C:\...\output\amdbc250_icd.json" /t REG_DWORD /d 0 /f
```

### Test
```cmd
cd output
test-gpu-ioctls.exe     # IOCTL test (13/15 pass)
test-vulkan-icd.exe     # Vulkan test (13/13 pass)
vulkaninfo.exe          # Official Vulkan test (passes!)
```

---

## What We Want to Do Next

### Immediate
1. **AllocVidMem** — fix MmAllocateContiguousMemory BSOD
2. **Display flip** — program HUBPREQ registers via KMD IOCTL
3. **Hardware init** — MMIO mapping, ring buffer, fence initialization in DriverEntry

### Short Term
4. **Real triangle rendering** — vertex buffer + PM4 draw commands
5. **ACO shader compilation** — DXBC/SPIR-V → GFX10 ISA
6. **D3D9 via IOCTL** — bypass D3D9On12 using custom path

### Medium Term
7. **Full WDDM display miniport** — DxgkInitialize + all DDI callbacks
8. **D3D11/D3D12 UMD** — functional stubs that actually work
9. **Multi-monitor** — 4 display pipes support

### Long Term
10. **OpenGL ICD** — Mesa radeonsi port
11. **Ray tracing** — RT core support
12. **GPU compute** — SDMA compute queue (when HW quirk is fixed)

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
│         ├── IOCTL 0x800008C4: FlipDisplay            │
│         ├── PM4 command ring → GPU                    │
│         └── EOP fence → completion signal             │
├─────────────────────────────────────────────────────┤
│              AMD BC-250 GPU (RDNA2/GFX1013)           │
│              24 CU, 16GB GDDR6, DCN 2.1              │
└─────────────────────────────────────────────────────┘
```

---

## File Structure

```
├── src/kmd/                     # Kernel-Mode Driver
│   ├── amdbc250_dream_v3_kmd.c  # IOCTL dispatch, DriverEntry, submit
│   ├── amdbc250_dream_v3_hw_init.c  # GPU init, ring buffers, display
│   ├── amdbc250_dream_v3_power.c    # Power/thermal management
│   ├── amdbc250_dream_v3_vm.c       # GPUVM, GART, page tables
│   └── dxgkrnl.def              # Import library (not in WDK)
├── src/umd/                     # User-Mode Driver
│   ├── amdbc250_umd_v46.c       # D3D9 DDI (45+ functions)
│   └── amdbc250_umd.def         # Export: OpenAdapter = ordinal 1
├── src/vulkan/                  # Vulkan ICD
│   ├── bc250_vulkan_icd.c       # 80+ Vulkan functions, IOCTL submit
│   ├── bc250_vulkan.def         # Export: vk_icdGetInstanceProcAddr
│   ├── bc250_aco_wrapper.c      # ACO shader compiler stub
│   └── bc250_shader.c           # SPIR-V → GFX10 ISA
├── test-tools/                  # Test applications
│   ├── test-gpu-ioctls.c        # IOCTL test (15 tests)
│   ├── test-vulkan-icd.c        # Vulkan test (13 tests)
│   └── test-render.c            # Rendering test
├── inf/                         # Driver INF
├── output/                      # Build output
├── build.bat                    # Build script
└── STATUS.md                    # Detailed project status
```

---

## License

Source code for educational purposes. Use at your own risk.
ACO compiler: MIT license (Mesa project).

---

*Made with love for GPU drivers. 🍖*

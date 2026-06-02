# AMD BC-250 Windows Driver Project

## Who We Are

AMD BC-250 Windows 11 driver project by Keshas & Kumpis (AI). Goal: fully working GPU driver for AMD BC-250 (PS5 motherboard "Ariel Root Complex") on Windows 11.

**Everyone welcome!** GPU drivers, WDDM, Vulkan experience — or just want to help.

---

## Hardware

- **SoC:** AMD BC-250 (PS5 Oberon variant) — 24 CU RDNA2, 16GB shared GDDR6
- **GPU ID:** 0x9FFF9700 (PS5 custom)
- **Memory:** CPU and GPU share GDDR6 (UMA) — VRAM at 0xC0000000
- **GPU BAR5:** 0xFE800000 (GPU MMIO registers)
- **NBIO:** PS5 NBIO firewall blocks selective GPU register ranges

---

## What We Discovered

### NBIO Firewall — Selective Register Protection

PS5 NBIO does NOT block all GPU access. It creates a **selective firewall** that blocks specific register ranges while allowing others:

**Registers we CAN read:**
- GPU_ID [0x0000] = 0x9FFF9700
- HDP registers [0x05A0-0x05DC] — Host Data Path config
- GC config [0x3000-0x3008] — Graphics Core configuration
- MMHUB [0x5000-0x59D0] — Memory Management Hub (42 registers, 7 identical VMHUB instances)
- DF [0x1A0E8-0x1A33C] — Data Fabric (40 registers)
- NBIO [0xC0D4-0x1FC] — NBIO internal config (19 registers)

**Registers we CAN write:**
- ✅ MMHUB [0x5000+] — VMHUB configuration
- ✅ GC config [0x3000+] — Graphics Core configuration

**Registers NBIO blocks (reads return 0xFFFFFFFF, writes ignored):**
- GRBM_STATUS [0x2004] — Graphics Register Bus Manager
- Scratch registers [0x2074+]
- CP (Command Processor) [0x2000-0x2FFF]
- CLK (Clock) [0x0D00-0x0DFF]
- UVD (Video Decoder) [0x2300+]
- SDMA (DMA Engine) [0x2600+]
- RSMU (System Management Unit) [0xA000+]
- HDP writes [0x05A0+] — reads work, writes ignored

### NBIO Signature Registers

Found two test/signature registers in NBIO block:
- `NBIO[0xC100]` = **0xFEDCBAEF** — magic test register
- `NBIO[0xC180]` = **0xFEDCBADF** — magic test register

These are likely used for NBIO self-test or unlock sequence.

### MMHUB Structure — 7 VMHUB Instances

MMHUB contains 7 identical blocks (spaced 0x180 apart), each with 6 registers:
- Offset 0x00: 0x80840000 (base config)
- Offset 0x1C: 0x00401000
- Offset 0x28: 0x00000100
- Offset 0x44: 0x00000004
- Offset 0xC4: 0x0000000F
- Offset 0xD0: 0x00004000

Each VMHUB instance controls a different physical memory domain (GPU, CPU, Display, etc.). Instances at: 0x5000, 0x5180, 0x5300, 0x5480, 0x5600, 0x5780, 0x5900.

### Data Fabric Registers

DF has 40 readable registers containing:
- Memory topology info (0x1A214-0x1A228: MMIO range descriptors)
- Port configuration (0x1A20C: 0x00001111, 0x1A210: 0x00010001)
- Test patterns (0x1A100: 0xAAAAAAAA, 0x1A26C: 0x55555555)
- Potential MMIO base: 0x1A304 = 0x20000000

### Other Discoveries

- `DxgkInitialize()` NOT exported by dxgkrnl.sys on Windows 11 26100 — WDDM miniport impossible
- IO port PCI config writes blocked by NBIO
- SMU/SMN access blocked from Windows
- BasicDisplay (Microsoft) handles the WDDM miniport role — our driver coexists as WDM control device
- Cold boot resets `HardwareInitialized` flag — must call INIT_HARDWARE before MMIO access

---

## Current Status (v4.3)

### Working

- ✅ **IOCTL channel** — full communication between user-mode and kernel driver
- ✅ **INIT_HARDWARE (Flags=1)** — maps BAR5 (0xFE800000) + BAR0 (0xC0000000) via MmMapIoSpace
- ✅ **READ_REG** — reads any GPU register via BAR5 MMIO
- ✅ **WRITE_REG** — writes to MMHUB and GC registers (NBIO allows)
- ✅ **GET_CAPS** — Version=430, GPUCLK=2000 MHz
- ✅ **GET_VRAM_INFO** — 16GB total VRAM (UMA shared)
- ✅ **GET_RESOURCE_BARS** — BAR addresses, MMIO state
- ✅ **ALLOC_VIDMEM** — contiguous memory allocation (PA returned)
- ✅ **WDDM coexistence** — BasicDisplay + our KMD on same DriverObject
- ✅ **Build + sign pipeline** — `build.bat` produces signed `atikmdag.sys`
- ✅ **No crashes** — safe-test approach (read-only + INIT_HARDWARE) works reliably
- ✅ **PSP v11 firmware loading** (C2PMSG mailbox, bootloader handshake)
- ✅ **Vulkan ICD** — 13/13 tests pass with official Vulkan loader
- ✅ **D3D9 UMD** — 45+ DDI functions, 5/5 adapter tests pass
- ✅ **IB packet + EOP fence**, GFX10 ring buffer, HDP Flush
- ✅ **SDMA copy/fill engine**, TDR reset, 40 CU unlock

### Known Limitations

- NBIO blocks GRBM/CP/CLK/Scratch register access — GPU command submission impossible
- Cannot initialize GFX ring buffer (requires CP register writes)
- Cannot submit PM4 commands (needs initialized ring buffer)
- VCN locked by Sony firmware
- DxgkDdiEscape unreachable via D3DKMTEscape — BasicDisplay handles it

---

## How to Build

### Prerequisites
- Visual Studio 2022 (Professional) — E:\Program Files\Microsoft Visual Studio\2022\Professional
- Windows WDK 10.0.26100.0 — E:\Program Files (x86)\Windows Kits\10
- Test signing: `bcdedit /set testsigning on` (Admin)
- Self-signed cert: `CN=AMD-BC250-Signer` in cert store

### Build Driver
```cmd
build.bat
```

### Build Tests
```cmd
test-tools\compile-wddm.bat      # Main WDDM+IOCTL test (S1-S24)
test-tools\compile-safe.bat      # Safe minimal test (read-only)
test-tools\compile-deep.bat      # Deep NBIO/DF/MMHUB scan + write test
```

### Install
1. `build.bat` → `output\atikmdag.sys`
2. Device Manager → AMD Radeon BC-250 → Update Driver → Browse → `output\`
3. Reboot

### Test
```cmd
output\safe-test.exe             # Safe test — no crashes, all read-only
output\deep-test.exe             # Deep register scan + write test
output\test-wddm.exe             # Full WDDM + IOCTL tests (S1-S24)
```

**IMPORTANT:** Run `safe-test.exe` first to verify driver is loaded. If `safe-test.exe` crashes, the driver needs reinstallation.

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│              User Applications                    │
├─────────────────────────────────────────────────┤
│         safe-test.exe / deep-test.exe             │
│         DeviceIoControl → \\.\AMDBC250DreamV43   │
├─────────────────────────────────────────────────┤
│         atikmdag.sys (KMD — WDM)                  │
│         ├── DriverEntry (manual g_PciDevExt)      │
│         ├── IRP_MJ_DEVICE_CONTROL handler         │
│         ├── INIT_HARDWARE (MMIO map, Flags=1)     │
│         ├── READ_REG / WRITE_REG (BAR5 MMIO)     │
│         ├── GET_CAPS / GET_VRAM_INFO              │
│         ├── ALLOC_VIDMEM (MDL-based)              │
│         ├── PSP v11 firmware loading              │
│         └── PM4 ring buffer (blocked by NBIO)     │
├─────────────────────────────────────────────────┤
│              NBIO Firewall                        │
│         ├── Allows: GPU_ID, HDP, GC, MMHUB, DF   │
│         ├── Writes: MMHUB ✅, GC ✅               │
│         └── Blocks: GRBM, CP, CLK, Scratch, RSMU │
├─────────────────────────────────────────────────┤
│              AMD BC-250 GPU (RDNA2)               │
│              24 CU, 16GB GDDR6, PSP v11          │
└─────────────────────────────────────────────────┘
```

---

## File Structure

```
├── src/kmd/                        # Kernel-Mode Driver
│   ├── amdbc250_dream_kmd.c        # DriverEntry, IOCTL dispatch, InitData
│   ├── amdbc250_dream_hw_init.c    # GPU init, ring buffers, display
│   ├── amdbc250_dream_power.c      # Power/thermal management
│   ├── amdbc250_dream_vm.c         # GPUVM, GART, page tables
│   └── amdbc250_psp_v11.c          # PSP firmware loading
├── src/umd/                        # User-Mode Driver
│   └── amdbc250_umd_v46.c          # D3D9 DDI (45+ functions)
├── src/vulkan/                     # Vulkan ICD
│   ├── bc250_vulkan_icd.c          # 80+ Vulkan functions
│   ├── bc250_aco_wrapper.c         # ACO shader compiler stub
│   └── bc250_shader.c              # SPIR-V → GFX10 ISA
├── inc/                            # Shared headers
│   ├── amdbc250_dream_kmd.h        # KMD structures, register offsets
│   ├── amdbc250_dream_hw.h         # Hardware register definitions
│   ├── amdbc250_psp_v11.h          # PSP context and API
│   └── amdbc250_ioctl.h            # IOCTL codes + structures
├── test-tools/                     # Test source + compile scripts
│   ├── safe-test.c                 # Safe minimal test (no crashes)
│   ├── deep-test.c                 # Deep NBIO/DF/MMHUB scan + write
│   ├── test-wddm.c                 # Full WDDM+IOCTL test (S1-S24)
│   ├── compile-safe.bat            # Compile safe-test
│   ├── compile-deep.bat            # Compile deep-test
│   └── compile-wddm.bat            # Compile test-wddm
├── output/                         # Build output (signed drivers)
│   ├── atikmdag.sys                # KMD (signed)
│   ├── amdbc250umd64.dll           # UMD
│   ├── amdbc250vulkan.dll          # Vulkan ICD
│   └── amdbc250_dream.inf          # Driver INF
├── build.bat                       # Build + sign driver
└── .gitignore                      # Excludes build artifacts, logs
```

---

## NBIO Firewall Register Map

### Readable Registers (confirmed)

| Block | Offset Range | Count | Description |
|-------|-------------|-------|-------------|
| GPU_ID | 0x0000 | 1 | SoC identification (0x9FFF9700) |
| HDP | 0x05A0-0x05DC | 2 | Host Data Path config |
| GC | 0x3000-0x3008 | 3 | Graphics Core config (WRITABLE) |
| MMHUB | 0x5000-0x59D0 | 42 | Memory Management Hub (WRITABLE) |
| DF | 0x1A0E8-0x1A33C | 40 | Data Fabric |
| NBIO | 0xC0D4-0x1FC | 19 | NBIO internal config |

### Blocked Registers (0xFFFFFFFF or ignored)

| Block | Offset Range | Description |
|-------|-------------|-------------|
| CLK | 0x0D00-0x0DFF | Clock configuration |
| GRBM | 0x2004 | Graphics Register Bus Manager |
| Scratch | 0x2074-0x207C | Scratch/test registers |
| CP | 0x2000-0x2FFF | Command Processor |
| UVD | 0x2300+ | Video Decoder |
| SDMA | 0x2600+ | DMA Engine |
| RSMU | 0xA000+ | System Management Unit |

### Writable Registers (confirmed)

| Register | Before | After | Notes |
|----------|--------|-------|-------|
| MMHUB[0x50D0] | 0x00004000 | 0x00004001 | VMHUB config |
| GC[0x3008] | 0x00000000 | 0x00000001 | Graphics Core config |
| HDP[0x05DC] | 0x00000000 | 0x00000000 | Writes ignored |
| SCRATCH[0x2074] | 0xFFFFFFFF | 0xFFFFFFFF | NBIO blocks |

---

## Roadmap

### Next — NBIO Unlock Investigation
1. **NBIO signature registers** — write sequences to 0xC100/0xC180 (0xFEDCBAEF/0xFEDCBADF)
2. **MMHUB VMHUB manipulation** — modify VMHUB config to remap blocked register space
3. **DF register analysis** — decode memory topology, find MMIO base 0x20000000
4. **Linux amdgpu analysis** — study how amdgpu handles NBIO on PS5/Ariel

### Short Term
5. **GRBM access** — find way to read GRBM_STATUS for GPU state
6. **CP ring buffer init** — once CP registers accessible, initialize command ring
7. **PM4 command submission** — submit draw/compute commands via ring buffer
8. **Real triangle rendering** — vertex buffer + PM4 draw

### Long Term
9. ACO shader compilation — DXBC/SPIR-V → GFX10 ISA
10. D3D9/D3D11 via IOCTL path
11. OpenGL ICD — Mesa radeonsi port
12. GPU compute — SDMA compute queue
13. Ray tracing — RT core support

---

## License

Source code for educational purposes. Use at your own risk.
ACO compiler: MIT license (Mesa project).

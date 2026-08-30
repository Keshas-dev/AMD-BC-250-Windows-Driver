# AMD Hybrid Driver Analysis (F:\AMD)

**Analyzed:** 2026-08-27
**Source:** F:\AMD (AMD Adrenalin 23.9.1 + Radeon ID Community + Amernime Zone)

## Overview

F:\AMD is a **hybrid AMD WDDM driver package** — the official AMD Adrenalin 23.9.1 driver repacked by **Radeon ID Community** (AMD-ID.com) with additional registry-level GPU parameter controls and custom installer infrastructure (Amernime Zone).

**Key facts:**
- **Not** an EFI DXE unlock tool
- **Not** a VBIOS mod
- **Not** a SPI_PG hardware register writer
- **Is** a driver-level registry parameter toolkit + custom INF + WDDM 3.1 driver

## Directory Structure

```
F:\AMD\
├── Bin64\                              # Tools, registry params, installer
│   ├── SetupBIN.cmd                    # Main installer (496 lines)
│   ├── ATIBINSetup.cmd                 # Driver install script
│   ├── ATIBINSetupCopilot.cmd          # FlexArch install script
│   ├── ATIBINSetupPro.cmd              # Enterprise install script
│   ├── ATIBINSetupVaxy.cmd             # Consumer install script
│   ├── ATIVaxySID.cmd                  # GPU Parameter Manager (6273 lines)
│   ├── ATIVaxyQuery.cmd                # GPU parameter reader (474 lines)
│   ├── ATIVaxyCalsMM.cmd               # Registry writer (52 lines)
│   ├── ATIVaxyCOM.cmd                  # Component registration
│   ├── ATIVaxyProxy.cmd                # Proxy driver config
│   ├── ATIVaxyLinked.cmd               # DX/OGL library switching
│   ├── ATIVaxySWC.cmd                  # Driver mode switching
│   ├── ATIVaxyPanel.cmd                # CCC UI installer
│   ├── ATIVaxyWMI.cmd                  # WMI provider
│   ├── ATIVaxyInit.cmd                 # Initialization
│   ├── cmdLibCoInst.cmd                # Infrastructure variables
│   ├── ATILib*.cmd                     # Library functions (COM, GPU, etc.)
│   ├── ATICMD*.cmd                     # Configuration modules
│   ├── ATIDSCleanUp*.cmd               # Driver cleanup
│   ├── ATIDSEnum*.cmd                  # Driver enumeration
│   ├── ATIHWID_R300_*.cmd              # Hardware ID tools
│   ├── dslink*.cmd                     # DSLink configuration
│   ├── symlink*.cmd                    # Symlink management
│   ├── glmesa*.cmd                     # Mesa/GL configuration
│   ├── MSFTInstall.cmd                 # WHQL fixer
│   ├── waves_kill.cmd                  # Audio process killer
│   ├── ConEmu\                         # Terminal emulator + registry .dat files
│   │   ├── CLSID\                      # 300+ .dat registry import files
│   │   ├── Elevated\                   # Elevated installers
│   │   ├── Far*_reg\                   # Far Manager registry
│   │   ├── Far*_fml\                   # Far Manager FML
│   │   ├── Far*_lua\                   # Far Manager Lua
│   │   ├── KeyEvents\                  # Key event handler
│   │   ├── Scripts\                    # Git scripts
│   │   └── wsl\                        # WSL bridge
│   ├── vendor\                         # 300+ .hive registry files
│   ├── pending\                        # Pending .nt parameter files
│   ├── plugins\                        # Plugins
│   └── assets\                         # Assets
├── MultiParse\                         # INF files
│   ├── u0395510_VanGogh_StockValve_2320.inf    # VanGogh INF
│   ├── u0395510_FireFlight_StockSubor_2320.inf  # FireFlight INF
│   └── sdi_display.cfg                 # SDI config
├── Packages\Drivers\Display\           # Driver packages
│   ├── WT6A_INF\                       # Main driver package
│   │   ├── bc250_vangogh.inf           # BC-250 INF (2026-08-26)
│   │   ├── u0395510.cat                # AMD catalog
│   │   └── B395473\                    # 200+ driver files
│   ├── amdaudio-VanGogh\               # VanGogh audio
│   ├── amdaudio-FireFlight\            # FireFlight audio
│   ├── amdfdans\                       # DANS
│   ├── amdxe\                          # XE
│   └── msft\                           # MSFT proxy/WHQL
├── Packages\SBDrv\                     # SB drivers
│   ├── AMDUCSI\                        # UCSI
│   └── Audio\                          # Audio drivers
└── Optional\                           # Optional components
    ├── amdfendr\                       # AMD firmware defender
    ├── amdpcibridge\                   # PCI bridge
    ├── RadeonLED\                      # LED control
    ├── RyzenMasterSDK\                 # Ryzen Master SDK
    └── USBCPDFW\                       # USB-C PD firmware
```

## WGPMode / CUMode — Registry-Level Flags

### What They Are

**WGPMode** and **CUMode** are **driver-level software flags** stored in Windows registry. They control how the AMD driver (atikmdag.sys / amdgpuv.sys) handles shader computation mode — NOT hardware power gating.

### Registry Keys Written

**WGPMode (Workgroup Processor):**

| Registry Path | Value | Meaning |
|--------------|-------|---------|
| `HKLM\SYSTEM\CurrentControlSet\Services\MSFTProxy\WMIC` | `"_0_"` / `"_1_"` / `"_2_"` | Auto / Off / On |
| `HKLM\SYSTEM\CurrentControlSet\Services\MSFTProxy\Enum` | `"Enabled"` / `"Disabled"` | Display name |
| `HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-...}\0000` | `ForceWGPmode` = dword:0/1 | Adapter level |
| `HKLM\...\0000\UMD` | `ForceWGPmode` = hex:30/31 | UMD level |
| `HKLM\...\0000\UMD\DXX` | `ForceWGPmode` = "0"/"1" | D3D12 |
| `HKLM\...\0000\UMD\DXC` | `ForceWGPmode` = "0"/"1" | DXC |
| `HKLM\...\0000\OpenGL\Private` | `ForceWGPmode` = dword:0/1 | OpenGL |
| `HKLM\SYSTEM\CurrentControlSet\Services\amdkmdag` | `ForceWGPmode` = dword:0/1 | Driver level |

**CUMode (Compute Unit):**

Same registry paths, different value names (`CUMode`, `ForceCUMode`, etc.)

### How They Work

```
ATIVaxySID.cmd (menu [1] WGP Mode / [30] CU Mode)
  → ATIVaxyCalsMM.cmd (prepare parameter)
    → REG.EXE IMPORT ConEmu\CLSID\GPUWorkGroupSMOn_0000.dat
      → Writes to Display Adapter registry (0000-0003 instances)
      → Writes to amdkmdag service registry
```

### .dat File Format

Files in `ConEmu\CLSID\` are **Windows Registry Editor Version 5.00** format:
- Unicode (UTF-16 LE) encoded
- Imported via `REG.EXE IMPORT`
- Each parameter has 4 variants: `_0000.dat`, `_0001.dat`, `_0002.dat`, `_0003.dat` (one per display adapter instance)

### What They Control

- **WGPMode ON** = Driver uses Workgroup Processor mode (2 CUs per WGP, RDNA architecture)
- **WGPMode OFF** = Driver uses legacy CU mode
- **CUMode** = Similar control for compute unit organization

### What They DON'T Control

- **DO NOT** write to `SPI_PG_ENABLE_STATIC_WGP_MASK` (0x5C3C) hardware register
- **DO NOT** change WGP power gating state
- **DO NOT** enable/disable WGPs at hardware level

## bc250_vangogh.inf Analysis

### What It Is

Custom INF for BC-250 (DEV_13FE) based on AMD Adrenalin 23.9.1 package. Created 2026-08-26.

### Hardware IDs

```
"AMD Radeon RX Navi Lite BC-250" = ati2mtag_NaviLite, PCI\VEN_1002&DEV_13FE&REV_00
"AMD Radeon RX Navi Lite" = ati2mtag_NaviLite, PCI\VEN_1002&DEV_13E9&REV_00
```

### Sections

- `[ati2mtag_NaviLite]` — main install section
- `[ati2mtag_NaviLite.Services]` — service registration (amdwddmg + External Events)
- `[ati2mtag_NaviLite.HW]` — hardware registry settings
- `[ati2mtag_NaviLite.Components]` — component registration (FDANS, UWP, WHQL, PROXY, VAXY)
- `[ati2mtag_NaviLite_SoftwareDeviceSettings]` — registry params (WmAgpMaxIdleClk, MemInitLatencyTimer, etc.)
- `[ati2mtag_NaviLite_PX]` — PowerXpress settings
- `[ati2mtag_NaviLite.GeneralConfigData]` — general configuration

### Code 43 Cause

1. **CAT hash mismatch**: INF modified (16299→26200) but `u0395510.cat` not updated → `0xE000024B: Driver package INF file hash is not present in catalog file`
2. **Hardware init failure**: Even if installed, `amdkmdag.sys` (94MB, 2023-08-26) tries to init GPU hardware (GFX ring, SDMA, DPM). BC-250 has SPI_PG=0 (WGPs off) and ring BASE SOS-locked → init fails → Code 43

## Registry Parameter System

### ATIVaxySID.cmd — GPU Parameter Manager

**6273 lines**, menu-driven interface with 300+ parameters organized into pages:

| Page | Category | Parameters |
|------|----------|------------|
| 1 | Profile Manager, Diagnostic, MultiGPU, Power, Display | 1-24 |
| 2 | Display [2/2], Driver Behavior | 1-30 |
| 3-7 | More parameter categories | ... |

### Key Parameters

| Parameter | Registry Value | Effect |
|-----------|---------------|--------|
| WGPMode | ForceWGPmode | Driver shader mode (WGP vs CU) |
| CUMode | ForceCUMode | Compute unit organization |
| ReBar | ReBarOn/Off | Resizable BAR |
| ULPS | ULPSOn/Off | Ultra Low Power State |
| PState | PStatePerformance | Performance state |
| HyperMemory | HyperMemOn/Off | HyperMemory |
| Workload | WorkloadGPUCompute | Graphics vs Compute workload |
| WaveSize | GPUWave32/64 | Wave32 vs Wave64 |
| SmartAccess | SmartAccessStorageOn/Off | Smart Access Storage |
| HBCC | HBCCOn/Off | High Bandwidth Cache Controller |
| ShaderCache | ShaderCacheOn/Off | Shader cache |
| StutterMode | StutterModeOn/Off | Stutter mode |
| Blockchain | KMD_BlockChain | Blockchain compute mode |

### Registry Write Mechanism

```
ATIVaxyCalsMM.cmd:
  For each adapter instance (0000-0003):
    1. Query if adapter exists (REG.EXE QUERY ... /v EnableULPS)
    2. If exists, import .dat file (REG.EXE IMPORT ...\GPUWorkGroupSMOn_0000.dat)
```

### .dat File Naming Convention

```
<ParameterName>_<Value>_<Instance>.dat

Examples:
  GPUWorkGroupSMOn_0000.dat    — WGP Mode ON, adapter 0
  GPUWorkGroupSMOff_0001.dat   — WGP Mode OFF, adapter 1
  GPUCUModeClr_0002.dat        — CU Mode Clear, adapter 2
  ReBarOn_0000.dat             — ReBar ON, adapter 0
```

## Comparison: Registry Flags vs Hardware SPI_PG

| Aspect | WGPMode Registry | SPI_PG_REGISTER (0x5C3C) |
|--------|-----------------|--------------------------|
| **Type** | Driver-level software flag | Hardware register |
| **Location** | Windows registry | BAR5 + 0x5C3C |
| **Written by** | ATIVaxySID.cmd / REG.EXE | Requires kernel driver or EFI DXE |
| **Controls** | Driver shader behavior | WGP power gating |
| **Effect on BC-250** | Driver reads value | Hardware is SOS-locked |
| **Persistence** | Survives reboot | Survives reboot (until cold boot) |
| **Reversibility** | Change registry + restart driver | Change register + restart driver |

## Installer Infrastructure

### SetupBIN.cmd (496 lines)

Main installer with menu:
1. **Standard Driver Profile** — Consumer mode (VanGogh, FireFlight)
2. **Universal CCC UI** — AMD Radeon Software Interface
3. **Extras** — Additional components
4. **Driver Update Config** — Windows Update driver management
5. **Kernel GPU Toolbox** — ATIVaxySID.cmd (GPU Parameter Manager)
6. **AMDKMDAG Graphics Vendor API** — DX/OGL library switching
7. **AMDKMDAG MiniPort Display Driver Control** — Driver mode switching
8. **WHQL Fixer** — Anti-cheat workaround

### ATIBINSetup.cmd (207 lines)

Driver installation script:
1. Copy INF to DriverStore
2. Choose install method (Microsoft Native / SDI)
3. Run library functions (ATILibCOM, ATILibCOMGen, etc.)
4. Configure PciBusReqDisa
5. Create scheduled task (StartCNHealth)
6. Register stub driver

### cmdLibCoInst.cmd (84 lines)

Infrastructure variables:
- `ENUM_ROOT` — Display adapter registry root
- `ENUM_0000` through `ENUM_0003` — Per-adapter registry paths
- `MSFTPROXY` — MSFTProxy registry path
- `MSFTWMIC` — MSFTProxy WMIC path
- `AMDKMDAG` — amdkmdag service registry path
- `CENUMQUERY` / `CENUMADD` / `CENUMIMPORT` — Registry tools
- `ENUMSID` — Path to .dat files

## DEV_13E9 vs DEV_13FE

### DEV_13E9 (PS5 VanGogh APU)

- Used in Steam Deck, some laptops
- May have different BIOS/VBIOS configuration
- SPI_PG may be unlocked at factory (WGP mask = 0x07 or 0x1F)
- Radeon ID Community has working driver for 13E9

### DEV_13FE (BC-250)

- Mining ASIC variant
- SPI_PG = 0x00000000 (all WGPs disabled)
- Ring BASE registers SOS-locked
- Requires EFI DXE unlock to write SPI_PG

### Why 13E9 May Work and 13FE Doesn't

1. **Different BIOS**: 13E9 may have VBIOS that initializes WGPs
2. **Different fuse config**: 13E9 may have SPI_PG unlocked
3. **EFI DXE unlock**: Radeon ID Community may use EFI DXE unlock for 13E9 before Windows boots

## What F:\AMD Does NOT Contain

- **No EFI DXE WGP unlock** — No .efi files, no DXE drivers
- **No VBIOS mod** — No .rom files, no VBIOS editor
- **No SPI_PG hardware writer** — No kernel driver that writes 0x5C3C
- **No firmware loader** — No PSP/SMU firmware loading tools
- **No Linux tools** — No bc250_smu_oc, no UMR, no debugfs

## What F:\AMD DOES Contain

- **Driver-level registry parameters** — 300+ flags for driver behavior
- **Custom INF files** — Modified for BC-250 and other ASICs
- **Registry import system** — .dat files for REG.EXE IMPORT
- **Parameter query system** — Read current registry state
- **Installer infrastructure** — SetupBIN, CoInst, ATILib, etc.
- **Mesa / OpenGL vendor switching** — `glmesa_dslink_main/cmd` + `ATIVaxyLinked.cmd` (34625 lines) + `gl_dslink_vndr.cmd` — switches ICD vendor (Mesa/RADV vs AMDVLK) via DSLink + devcon disable/enable
- **ConEmu terminal** — Console emulator with WSL bridge
- **Snappy Driver Installer** — Alternative driver installer

## Implications for BC-250 Project

### What Registry Parameters Can Do

1. **Optimize driver behavior** — WGP/CU mode, ReBar, ULPS, etc.
2. **Enable compute modes** — WorkloadGPUCompute, Blockchain mode
3. **Configure display** — Dithering, color, HDR, etc.
4. **Power management** — P-state, sleep, etc.

### What Registry Parameters Cannot Do

1. **Enable WGPs** — SPI_PG is hardware-locked
2. **Fix ring BASE** — SOS-locked
3. **Load firmware** — PSP/SMU firmware not loaded by registry
4. **Fix Code 43** — Hardware init failure requires hardware unlock

### Potential Uses

1. **Driver optimization** — Once WGPs are unlocked (via EFI DXE), registry params can optimize WGP/CU mode
2. **Compute workload** — Registry params can optimize for compute once hardware is unlocked
3. **Display configuration** — Registry params can configure display output
4. **Power management** — Registry params can manage power states

## Key Files Reference

| File | Lines | Purpose |
|------|-------|---------|
| SetupBIN.cmd | 496 | Main installer |
| ATIBINSetup.cmd | 207 | Driver install script |
| ATIVaxySID.cmd | 6273 | GPU Parameter Manager |
| ATIVaxyQuery.cmd | 474 | GPU parameter reader |
| ATIVaxyCalsMM.cmd | 52 | Registry writer |
| cmdLibCoInst.cmd | 84 | Infrastructure variables |
| ATILibCOM.cmd | 22 | Certificate installation |
| ATIVaxyCOM.cmd | 10648 | Component registration |
| ATIVaxyProxy.cmd | 4179 | Proxy driver config |
| ATIVaxyLinked.cmd | 34625 | DX/OGL library switching |
| ATIVaxySWC.cmd | 10449 | Driver mode switching |
| ATIVaxyPanel.cmd | 28595 | CCC UI installer |
| ATIVaxyWMI.cmd | 2399 | WMI provider |
| ATIVaxyInit.cmd | 9297 | Initialization |
| ATIVaxyCopilot.cmd | 39539 | Copilot (FlexArch) |

## Registry Parameter Files (.dat)

Located in `F:\AMD\Bin64\ConEmu\CLSID\`:

| Category | Count | Examples |
|----------|-------|----------|
| GPU Culling | 8 | GPUCullingBackFace, GPUCullingFrustum, etc. |
| GPU Buffering | 10 | Buff_D3DTriple, Buff_DXXQuad, Buff_OGLQuad, etc. |
| GPU Compute | 5 | GPUWorkGroupSM, GPUCUMode, GPUComputeShader, etc. |
| GPU Power | 6 | OSGPUPW, FullScrPwr, OverdriveSW, etc. |
| GPU Memory | 4 | HyperMem, SmartAccessStorage, HBCC, etc. |
| GPU Configuration | 10+ | ReBar, ULPS, PState, StutterMode, etc. |
| Display | 6 | Dithering, Color, HDR, VSync, etc. |
| Driver | 8 | AMDKMD, AMDDRS, ShaderCache, etc. |
| **Total** | **300+** | |

## Conclusion

F:\AMD is a **driver-level optimization toolkit** — it provides registry-level control over AMD driver behavior but **cannot** modify hardware state (SPI_PG, ring BASE, etc.). For BC-250, registry parameters are useful **only after** WGPs are unlocked via EFI DXE or other hardware-level method.

The package is valuable for:
1. Understanding AMD driver registry parameters
2. Driver optimization once hardware is unlocked
3. INF structure reference for custom drivers
4. Registry import/export methodology

But it is **not** a solution for BC-250 WGP unlock.

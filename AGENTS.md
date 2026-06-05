# AMD BC-250 Windows Driver — Agent Notes

## Build
- Run `build.bat` (requires VS2022 + WDK 10.0.26100.0, auto-detects E: or C:)
- KMD: compiled with `/kernel`, linked `/DRIVER /ENTRY:DriverEntry`; output `output\atikmdag.sys`
- UMD: compiled as `/TP` (C++), linked `/DLL`; output `output\amdbc250umd64.dll`
- Only **2 of 4** KMD `.c` files are in the build: `amdbc250_dream_kmd.c` + `amdbc250_dream_hw_init.c`
- `amdbc250_dream_vm.c` and `amdbc250_dream_power.c` exist but are **NOT** compiled; to add them, edit `src\kmd\SOURCES` and add to `build.bat`
- Test-signing required: `bcdedit /set testsigning on` + reboot; cert `CN=AMD-BC250-Signer` must be in store

## Install / Deploy
- **Always uninstall first**: Device Manager → Uninstall AMD Radeon BC-250 (check "Delete driver") → **Reboot** → Update Driver → browse to `output\` → **Reboot**
- `deploy-driver.bat` copies to `%SystemRoot%\System32\drivers\atikmdag.sys` and starts service; requires Admin
- `tools\register-icd.bat` registers Vulkan ICD

## Architecture Quirks
- **No WDDM miniport** (`DxgkInitialize()` not exported on Win11 26100). Driver coexists as **WDM control device** alongside Microsoft BasicDisplay.
- Device name: `\\.\AMDBC250DreamV43` (IOCTL path)
- NBIO firewall blocks GRBM/CP/CLK/Scratch registers; MMHUB/GC are writable. GPU command submission impossible without PSP unlock.
- **C2PMSG_35/36 (PSP bootloader) return 0xFFFFFFFF from Windows** — NBIO blocks them. C2PMSG_64 is writable as a substitute but PSP bootloader doesn't listen to it.
- `INIT_HARDWARE` IOCTL (0x80000840) with `Flags=1` maps BAR5 (0xFE800000) via `MmMapIoSpace` **without** full GPU init; required before any register access.
- Cold boot resets `HardwareInitialized` flag; always call `INIT_HARDWARE` first.
- AllocVidMem uses **MDL-based** `MmAllocatePagesForMdlEx` (not contiguous); FreeVidMem requires both VA and MDL handle from output.
- **Doorbell[0x124] is writable** from Windows (confirmed). Can be used with C2PMSG_64 for experimentation but doesn't trigger PSP bootloader.

## NBIO Firewall — Proven Dead Ends
- Writing to root complex PCI config offset 0xB8 **does NOT unlock NBIO** (writes persist but no effect)
- Writing to GPU or PSP device PCI config offset 0xB8 **does NOT unlock NBIO**
- SMC Index/Data indirect access (0x1B8/0x1BC) **does NOT bypass** NBIO
- SMN access **returns 0x00000000** — different from 0xFFFFFFFF but not usable
- C2PMSG_64 + Doorbell **does NOT trigger** PSP bootloader (correct: bootloader only reads C2PMSG_35/36)
- **NBIO firewall = dynamic address protection** similar to IOMMU page tables. Only PSP SOS can disable it from the inside.

## PSP Firmware
- `output/cyan_skillfish2_sos_extracted.bin` (262KB) — extracted from `third-party/Bios/BC250_3.00_CHIPSETMENU.ROM`
- PSP directory at ROM offset 0x008E0000 (`$PSP` header), SOS type=0x08
- ASD firmware: 1088 bytes, TA firmware: 262KB
- `src/kmd/firmware_data.h` contains embedded firmware in `g_SosFirmwareData[]` (compiled into atikmdag.sys)

## EFI Shell Injection (Boot-Time PSP Firmware Loading)
Since NBIO blocks C2PMSG_35/36 from Windows, firmware must be loaded **before Windows starts** via UEFI Shell.

**Files in `uefi/output/`:**
- `test_psp.nsh` — reads C2PMSG_81/35/36 to verify register accessibility at boot
- `inject_psp.nsh` — writes firmware address to C2PMSG_36, LOAD_SOS to C2PMSG_35
- `full_inject.nsh` — full sequence: test + inject + verify
- `cyan_skillfish2_sos_extracted.bin` — PSP SOS firmware (copy to USB)

**Register map for EFI Shell (physical addresses):**
| Register | BAR5 offset | Physical addr | Purpose |
|----------|-------------|---------------|---------|
| C2PMSG_35 | 0x5818C | 0xFE85818C | Bootloader command (write LOAD_SOS=0x20000000) |
| C2PMSG_36 | 0x58190 | 0xFE858190 | Firmware address (write TMR_phys >> 20) |
| C2PMSG_64 | 0x58200 | 0xFE858200 | TOS mailbox / status |
| C2PMSG_81 | 0x58244 | 0xFE858244 | Sign of Life (bit31 = SOS alive) |

**UEFI Shell mm syntax:**
- Read: `mm 0xFE858244 -mm -b 4`
- Write: `mm 0xFE858190 0x000007E4 -mm -b 4`
- Wait: `stall 1000000` (1 second)

**Expected mm output:** Shows hex values. C2PMSG_35/36 should NOT be 0xFFFFFFFF at EFI boot time.

## Test Tools
- Individual `compile-*.bat` scripts in `test-tools\`; all hardcode `E:\Program Files\Microsoft Visual Studio\2022\Professional\...`
- Run order after install: `safe-test.exe` first (read-only, no crashes), then `deep-test.exe`, then `test-wddm.exe`
- `test-driver-check.exe` tests new IOCTLs: GPU_INFO, FIREWALL_STATUS, REG_TEST
- `test-gpu-ioctls.exe`: 14/15 PASS; `test-vulkan-icd.exe`: 13/13 PASS; `test-d3d9-adapter.exe`: 5/5 PASS
- `test-psp-loader.exe` — C2PMSG sequence test (runs from Windows, confirms C2PMSG_64 writable)
- `test-nbio-pci-root.exe` — PCI config 0xB8 test (confirms writes persist but NBIO stays locked)

## Source Layout
- `src\kmd\` — Kernel driver (only kmd.c + hw_init.c active)
- `src\umd\` — D3D9 UMD (`amdbc250_umd_v46.c`, 45+ DDI functions)
- `src\vulkan\` — Vulkan ICD (`bc250_vulkan_icd.c`, 80+ stubs)
- `inc\` — Shared headers; `amdbc250_ioctl.h` has all IOCTL codes and structs
- `inf\` — Driver INF (`amdbc250_dream.inf`)
- `docs\` — NBIO firewall analysis, Linux amdgpu PSP analysis
- `uefi\` — EFI Shell scripts for boot-time PSP injection

## Registry / Features
- 40 CU unlock: `reg add HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43 /v Enable40CU /t REG_DWORD /d 1` + reboot
- PSP v11 is compiled **into** `atikmdag.sys`; no separate PSP driver (old code commented in build.bat)

## Gotchas
- VS paths in compile scripts are hardcoded to `E:`; may need editing if VS is on `C:`
- `build.bat` pauses on errors; non-interactive use may hang
- MMIO mapping can fail if SMU blocks access; this is expected on some configurations
- `vkFreeMemory` must NOT pass VirtualAlloc addresses to `MmFreeContiguousMemory` (BSOD)
- `test-render.c` historically passed PA instead of VA to FREE_DMA_BUFFER (BSOD)

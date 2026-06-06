# AMD BC-250 Windows Driver — Agent Notes

## Two-Project Architecture (Hybrid Model)
- **GPU Driver** (this project): `atikmdag.sys` — WDM control device, IOCTL channel, Vulkan/D3D9
- **PSP Driver** (separate project): `PspDriver.sys` — PSP firmware loading, ring creation, NBIO unlock
  - GitHub: https://github.com/Keshas-dev/AMD-BC-250-PSP-Driver
  - Local: `C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver`
- The two drivers run in parallel. GPU driver uses 512KB BAR5 mapping; PSP driver uses 2MB BAR5 mapping for PSP register access.
- After PSP driver unlocks NBIO, GPU driver detects it via IOCTL_GET_NBIO_STATUS and can access GRBM/CP registers.

## Build
- Run `build.bat` (requires VS2022 + WDK 10.0.26100.0, auto-detects E: or C:)
- KMD: compiled with `/kernel`, linked `/DRIVER /ENTRY:DriverEntry`; output `output\atikmdag.sys`
- UMD: compiled as `/TP` (C++), linked `/DLL`; output `output\amdbc250umd64.dll`
- Only 2 of 4 KMD `.c` files are active: `amdbc250_dream_kmd.c` + `amdbc250_dream_hw_init.c`
- Test-signing required: `bcdedit /set testsigning on` + reboot; cert `CN=AMD-BC250-Signer`

## Install
- **Always uninstall first**: Device Manager → Uninstall (check "Delete driver") → **Reboot** → Update Driver → browse to `output\` → **Reboot**

## Architecture Quirks
- **No WDDM miniport** — driver is WDM control device alongside Microsoft BasicDisplay
- Device path: `\\.\AMDBC250DreamV43`
- NBIO firewall blocks GRBM/CP/CLK/Scratch registers (return 0xFFFFFFFF)
- C2PMSG_35/36 blocked by NBIO from Windows; C2PMSG_64 and Doorbell[0x124] are writable
- `INIT_HARDWARE` IOCTL (Flags=1) maps BAR5 at 512KB for register access
- AllocVidMem uses MDL-based `MmAllocatePagesForMdlEx` (not contiguous)

## PSP Integration
- PSP firmware extracted from `third-party/Bios/BC250_3.00_CHIPSETMENU.ROM`
- PSP SOS firmware: `output/cyan_skillfish2_sos_extracted.bin` (262KB)
- Real PSP MP0 base: ~0x40F8 (NOT 0x16000 — that was a false positive)
- PSP register offsets: C2PMSG_35=0x1056C, C2PMSG_36=0x10570, C2PMSG_81=0x10614
- These offsets require 2MB BAR5 mapping (beyond 512KB GPU BAR5)
- Signal to check for PSP SOS via IOCTL_GET_NBIO_STATUS (0x80000C0C)

## IOCTL Reference
| IOCTL | Code | Purpose |
|-------|------|---------|
| INIT_HARDWARE | 0x80000B80 | Map BAR5/BAR0 MMIO |
| READ_REG | 0x80000B88 | Read GPU register |
| WRITE_REG | 0x80000B8C | Write GPU register |
| ALLOC_VIDMEM | 0x80000840 | Allocate system memory |
| GET_GPU_INFO | 0x80000C00 | GPU info (vendor, CUs, etc) |
| GET_FIREWALL_STATUS | 0x80000C04 | NBIO firewall block counts |
| GET_NBIO_STATUS | 0x80000C0C | Live SOS/GRBM/CP check |
| PSP_GET_STATUS | 0x80000BA4 | PSP context (cached) |

## Test Tools
- `safe-test.exe` — read-only, no crashes (run first after install)
- `deep-test.exe` — register scan + write test
- `test-wddm.exe` — full WDDM + IOCTL tests
- `test-driver-check.exe` — GPU_INFO, FIREWALL, REG_TEST IOCTLs
- `nbio-monitor.exe` — polls GRBM every second (wait for PSP unlock)
- All tests use `\\.\AMDBC250DreamV43` device path

## Source Layout
- `src\kmd\` — Kernel driver (kmd.c + hw_init.c + psp_v11.c + vm.c + power.c)
- `src\umd\` — D3D9 UMD (amdbc250_umd_v46.c)
- `src\vulkan\` — Vulkan ICD (bc250_vulkan_icd.c)
- `inc\` — Shared headers (ioctl.h, psp_v11.h, kmd.h, hw.h)
- `docs\` — NBIO firewall analysis, Linux PSP analysis
- `uefi\output\` — EFI Shell boot-time injection scripts
- `test-tools\` — Test source + compile scripts

## Gotchas
- VS paths in compile scripts hardcoded to `E:`; edit if VS is on `C:`
- `build.bat` pauses on errors; may hang in non-interactive use
- MMIO mapping can fail if SMU blocks access
- vkFreeMemory must NOT pass VirtualAlloc addresses to MmFreeContiguousMemory (BSOD)
- test-render.c historically passed PA instead of VA (BSOD)
- Do NOT map BAR5 beyond 512KB — crashes the system
- PSP registers at 0x1056C+ require 2MB BAR5 mapping via PSP driver
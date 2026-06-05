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
- `INIT_HARDWARE` IOCTL (0x80000840) with `Flags=1` maps BAR5 (0xFE800000) via `MmMapIoSpace` **without** full GPU init; required before any register access.
- Cold boot resets `HardwareInitialized` flag; always call `INIT_HARDWARE` first.
- AllocVidMem uses **MDL-based** `MmAllocatePagesForMdlEx` (not contiguous); FreeVidMem requires both VA and MDL handle from output.

## Test Tools
- Individual `compile-*.bat` scripts in `test-tools\`; all hardcode `E:\Program Files\Microsoft Visual Studio\2022\Professional\...`
- Run order after install: `safe-test.exe` first (read-only, no crashes), then `deep-test.exe`, then `test-wddm.exe`
- `test-driver-check.exe` tests new IOCTLs: GPU_INFO, FIREWALL_STATUS, REG_TEST
- `test-gpu-ioctls.exe`: 14/15 PASS; `test-vulkan-icd.exe`: 13/13 PASS; `test-d3d9-adapter.exe`: 5/5 PASS

## Source Layout
- `src\kmd\` — Kernel driver (only kmd.c + hw_init.c active)
- `src\umd\` — D3D9 UMD (`amdbc250_umd_v46.c`, 45+ DDI functions)
- `src\vulkan\` — Vulkan ICD (`bc250_vulkan_icd.c`, 80+ stubs)
- `inc\` — Shared headers; `amdbc250_ioctl.h` has all IOCTL codes and structs
- `inf\` — Driver INF (`amdbc250_dream.inf`)
- `docs\` — NBIO firewall analysis, Linux amdgpu PSP analysis

## Registry / Features
- 40 CU unlock: `reg add HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43 /v Enable40CU /t REG_DWORD /d 1` + reboot
- PSP v11 is compiled **into** `atikmdag.sys`; no separate PSP driver (old code commented in build.bat)

## Gotchas
- VS paths in compile scripts are hardcoded to `E:`; may need editing if VS is on `C:`
- `build.bat` pauses on errors; non-interactive use may hang
- MMIO mapping can fail if SMU blocks access; this is expected on some configurations
- `vkFreeMemory` must NOT pass VirtualAlloc addresses to `MmFreeContiguousMemory` (BSOD)
- `test-render.c` historically passed PA instead of VA to FREE_DMA_BUFFER (BSOD)

# BC-250 Windows Driver — Install Guide (Admin Required)
# Generated: 2026-09-15

## Prerequisites

- Windows 11 26100 (or compatible)
- Administrator privileges
- Test Signing mode ON: `bcdedit /set testsigning on`
- Secure Boot OFF (BIOS/UEFI)
- Reboot after enabling test signing

## Before Installing

### 1. Uninstall Old GPU Driver
```
Device Manager → Right-click "AMD Radeon BC-250" → Uninstall device
Check "Delete the driver software for this device"
Reboot
```

### 2. Verify Firmware Files Exist
These files must exist for the driver to function:
```
C:\Windows\System32\drivers\bc-250\Sysdrv.bin
C:\Windows\System32\drivers\bc-250\Sos.bin
C:\Windows\System32\drivers\bc-250\Smu.bin
C:\Windows\System32\drivers\bc-250\cyan_skillfish2_*.bin
```
If missing, copy from `output\firmware\` (in repo).

## Installation Steps

### Step 1: Build (from VS2022 + WDK admin prompt)
```cmd
:: Open VS2022 x64 Native Tools + Admin prompt
:: VS2022 is at F:\Program Files\Microsoft Visual Studio\2022\Community

cd C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main
build.bat
```
This builds and signs `output\atikmdag.sys`, `output\amdbc250umd64.dll`, INF, CAT.

### Step 2: Install GPU Driver
```cmd
:: In Device Manager → Update driver → Browse → select output\amdbc250_dream.inf
:: OR from command line:
pnputil /add-driver "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_dream.inf" /install
```
Then reboot.

### Step 3: Verify Installation
After reboot:
```cmd
:: Check driver loaded
sc query atikmdag
:: Should show STATE = 4 (RUNNING)

:: Check Device Manager
devmgmt.msc → "AMD Radeon BC-250" → Status OK

:: Check VRAM info (requires Vulkan SDK)
set VULKAN_SDK=F:\VulkanSDK\1.4.341.1
set VK_ICD_FILENAMES=C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_icd.json
F:\VulkanSDK\1.4.341.1\Bin\vulkaninfoSDK.exe --summary
:: Should show GPU0 AMD BC-250, 16GB VRAM
```

## Build Commands (Manual — if build.bat fails)

### KMD (atikmdag.sys)
```cmd
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

cl /c /kernel /W3 /Zi /Od /DAMD64 /D_AMD64_ /DAMDBC250_DREAM_V3 ^
  /I"F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\km" ^
  /I"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc" ^
  /I"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd" ^
  amdbc250_dream_kmd.c amdbc250_dream_hw_init.c ^
  amdbc250_dream_hw_init_extended.c amdbc250_dream_power.c ^
  amdbc250_dream_vm.c amdbc250_psp.c ^
  amdbc250_dream_fw_load.c amdbc250_dream_psp_fw_load.c ^
  amdbc250_dream_golden.c amdbc250_dream_hdp.c ^
  amdbc250_dream_rlc.c amdbc250_dream_vbios.c ^
  amdbc250_dream_kmd_ddi_stubs.c

link /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
  /OUT:atikmdag.sys ^
  amdbc250_dream_kmd.obj amdbc250_dream_hw_init.obj ^
  amdbc250_dream_hw_init_extended.obj amdbc250_dream_power.obj ^
  amdbc250_dream_vm.obj amdbc250_psp.obj ^
  amdbc250_dream_fw_load.obj amdbc250_dream_psp_fw_load.obj ^
  amdbc250_dream_golden.obj amdbc250_dream_hdp.obj ^
  amdbc250_dream_rlc.obj amdbc250_dream_vbios.obj ^
  amdbc250_dream_kmd_ddi_stubs.obj ^
  ntoskrnl.lib wdm.lib win32k.lib ntstrsafe.lib ^
  BufferOverflowK.lib hal.lib displib.lib ^
  /LIBPATH:"F:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\km\x64"
```

### ICD Stub (bc250_icd_stub.dll)
Use existing binary at `output\bc250_icd_stub.dll`. To rebuild:
```cmd
:: Fix compile-bc250vulkan.bat: add Vulkan SDK include
:: Add /I"F:\VulkanSDK\1.4.341.1\Include\vulkan" to INCLUDE
:: Add #include <vulkan/vulkan.h> before bc250_vulkan.h in bc250_vulkan_icd.c

call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\vulkan"
compile-bc250vulkan.bat
```

### KMDOD Display Driver (optional, separate)
```cmd
:: From KMDOD directory:
F:\bc-250-proektas\Dev\windows-driver-samples\video\KMDOD\Sample\build_kmdod.bat
:: Or use the latest build in output\kmdod-test\
```

## Post-Installation Verification

### Check Driver
```cmd
sc query atikmdag
driverquery | findstr atikmdag
```

### Check Vulkan
```powershell
$env:VULKAN_SDK = "F:\VulkanSDK\1.4.341.1"
$env:VK_ICD_FILENAMES = "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_icd.json"
& "F:\VulkanSDK\1.4.341.1\Bin\vulkaninfoSDK.exe" --summary
```
Expected: `GPU0: AMD BC-250 (RADV Stub), api=1.2, VRAM=16GB`

### Test Vulkan Pipeline
```powershell
# Run our test (compiles via compile-wddm.bat in test-tools\)
& "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\vk-minimal-test.exe"
```

### Check SMU
```cmd
# SMU monitor (runs continuously, Ctrl+C to stop)
& "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\smu-monitor.exe"

# One-shot SMU query
& "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\smu-telemetry-cli.exe" --once
```

### Check Temperature
```powershell
# Via KMD IOCTL (in test-tools\)
& "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-gpu-ioctls.exe"
```

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| Code 43 | Driver init crash | Check DriverEntry, verify firmware files exist |
| Code 37 | WDDM init failure | Driver is WDM not WDDM; install as "Legacy Driver" |
| No Vulkan device | ICD not registered | Check `HKLM\SOFTWARE\Khronos\Vulkan\Drivers`, set `VK_ICD_FILENAMES` |
| 4GB VRAM shown | Old KMD | Reinstall KMD (uninstall → reboot → install new) |
| BSOD 0x1A | GART/VM init | Ensure `HwInitGart=0`, `HwInitVm=0` in registry |
| BSOD 0x3B | Display init | Ensure `DisplayWritesEnabled=0` (safety), DCN not initialized |
| Test signing warning | Cert not trusted | `bcdedit /set testsigning on`, Secure Boot OFF |

## Uninstall
```cmd
:: Remove driver
pnputil /delete-driver oemXXX.inf /force
:: Or Device Manager → Uninstall → Delete driver
:: Reboot
```

## Registry Keys (driver parameters)
```
HKLM\SYSTEM\CurrentControlSet\Services\atikmdag\Parameters
    HwInitGart=0        (don't init GART — causes 0x1A)
    HwInitVm=0          (don't init VM — causes 0x1A)
    HwInitGfxRing=0     (don't init GFX ring — SOS-locked)
    HwInitSdmaRing=0    (don't init SDMA ring — SOS-locked)
    HwUnhaltCp=0        (don't unhalt CP — rogue DMA risk)
    HwInitMaxStep=0     (0=run all, N=cap at step N)
    DisplayWritesEnabled=0  (safety: disable display reg writes)
```

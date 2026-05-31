# Quick Start - AMD BC-250 Dream Drivers v4.3

## 1. Build

```cmd
cd AMD-BC-250-Windows-Driver-main
build.bat
```

Output: `output/` (atikmdag.sys, amdbc250umd64.dll, amdbc250vulkan.dll, .cat, .inf)

## 2. Enable Test Signing

Run as Administrator:
```powershell
bcdedit /set testsigning on
```
Reboot required.

## 3. Install Driver

1. Open **Device Manager**
2. Right-click display adapter > **Update Driver**
3. **Browse my computer for drivers**
4. Browse to: `AMD-BC-250-Windows-Driver-main\output`
5. Select: **AMD Radeon BC-250 Graphics (Dream Drivers v4.3)**

## 4. Verify

```powershell
Get-PnpDevice -Class Display | Select Status, FriendlyName
```

Expected: `Status = OK/Degraded`, `FriendlyName = AMD Radeon BC-250 Graphics`

## 5. Enable 40 CUs (optional, 1.61x boost)

```powershell
# Run as Admin
reg add "HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43" /v Enable40CU /t REG_DWORD /d 1
# Reboot
```

## 6. Register Vulkan ICD

```cmd
tools\register-icd.bat
```

## 7. Test

```cmd
test-tools\test-gpu-ioctls.exe      # IOCTL test (14/15 pass)
test-tools\test-vulkan-icd.exe      # Vulkan ICD test (13/13 pass)
test-tools\test-gpu-hw-init.exe     # Hardware init test (5/7 pass)
test-tools\test-d3d9-adapter.exe    # D3D9 adapter test (5/5 pass)
vulkaninfo.exe                      # Official Vulkan test (passes!)
```

## Uninstall

```powershell
tools\uninstall-bc250.ps1
```
Or Device Manager > Uninstall device.

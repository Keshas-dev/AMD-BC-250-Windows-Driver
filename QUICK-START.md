# Quick Start - AMD BC-250 Dream Drivers v4.3

## 1. Build

```cmd
cd AMD-BC-250-Windows-Driver-main
build.bat
```

Output: `output/` (atikmdag.sys, amdbc250umd64.dll, .cat, .inf)

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

## 5. Test IOCTL Communication

```cmd
test-tools\test-gpu-ioctls.exe
```

Runs 15 tests: GetCaps, VRAM, Temperature, Display, Memory, SDMA, Fence, TDR, EDID, Shader.

## Uninstall

```powershell
tools\uninstall-bc250.ps1
```
Or Device Manager > Uninstall device.

# Quick Start - AMD BC-250 Dream Drivers

## 1. Build

```cmd
cd AMD-BC-250-Windows-Driver-main
build.bat
```

Output: `output/atikmdag.sys`, `output/amdbc250umd64.dll`

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

Expected: `Status = OK`, `FriendlyName = AMD Radeon BC-250 Graphics`

## Uninstall

```powershell
# PowerShell (run as admin)
tools\uninstall-bc250.ps1
```

Or Device Manager > Uninstall device.

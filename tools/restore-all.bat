@echo off
echo ========================================
echo  Restore All Windows Changes
echo ========================================
echo.
echo This script restores ALL Windows changes made for BC-250.
echo.
pause

echo.
echo [1/7] Removing advanced boot options (F8 menu fix)...
bcdedit /set bootmenupolicy Standard
if %errorlevel% equ 0 (echo   OK) else (
  echo   Trying alternative method...
  bcdedit /deletevalue advancedoptions
  if %errorlevel% equ 0 (echo   OK) else echo   FAILED
)

echo.
echo [2/7] Restoring Hypervisor...
bcdedit /set hypervisorlaunchtype Auto
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [3/7] Restoring integrity checks...
bcdedit /set nointegritychecks off
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [4/7] Restoring Memory Integrity (HVCI)...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity" /v Enabled /t REG_DWORD /d 1 /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [5/7] Restoring VBS...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard" /v EnableVirtualizationBasedSecurity /t REG_DWORD /d 1 /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [6/7] Restoring Secure Boot...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\SecureBoot\State" /v UEFISecureBootEnabled /t REG_DWORD /d 1 /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [7/7] Restoring AMD PSP Device...
reg add "HKLM\SYSTEM\CurrentControlSet\Services\amdpsp" /v Start /t REG_DWORD /d 3 /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo ========================================
echo  ALL CHANGES RESTORED!
echo ========================================
echo.
echo Next steps:
echo 1. Reboot computer
echo 2. Install new driver
echo 3. Test
echo.
pause
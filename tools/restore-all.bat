@echo off
echo ========================================
echo  Restore All Windows Changes
echo ========================================
echo.
echo This script restores ALL Windows changes made for BC-250.
echo.
pause

echo.
echo [1/6] Restoring Hypervisor...
bcdedit /set hypervisorlaunchtype Auto
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [2/6] Restoring integrity checks...
bcdedit /set nointegritychecks off
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [3/6] Restoring Memory Integrity (HVCI)...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity" /v Enabled /t REG_DWORD /d 1 /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [4/6] Restoring VBS...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard" /v EnableVirtualizationBasedSecurity /t REG_DWORD /d 1 /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [5/6] Restoring Secure Boot...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\SecureBoot\State" /v UEFISecureBootEnabled /t REG_DWORD /d 1 /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [6/6] Restoring AMD PSP Device...
reg add "HKLM\SYSTEM\CurrentControlSet\Services\amdpsp" /v Start /t REG_DWORD /d 3 /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo ========================================
echo  ALL CHANGES RESTORED!
echo ========================================
echo.
echo Next steps:
echo 1. Reboot computer
echo 2. Install new driver (after fixes)
echo 3. Test PSP
echo.
pause
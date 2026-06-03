@echo off
echo ========================================
echo  VBS/HVCI Restore Script for BC-250
echo ========================================
echo.
echo This script restores VBS/HVCI settings to default.
echo.
pause

echo.
echo [1/4] Restoring Memory Integrity (HVCI)...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity" /v Enabled /t REG_DWORD /d 1 /f
if %errorlevel% equ 0 (
    echo   OK - Memory Integrity enabled
) else (
    echo   FAILED - run as Administrator
)

echo.
echo [2/4] Restoring VBS...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard" /v EnableVirtualizationBasedSecurity /t REG_DWORD /d 1 /f
if %errorlevel% equ 0 (
    echo   OK - VBS enabled
) else (
    echo   FAILED - run as Administrator
)

echo.
echo [3/4] Restoring Secure Boot...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\SecureBoot\State" /v UEFISecureBootEnabled /t REG_DWORD /d 1 /f
if %errorlevel% equ 0 (
    echo   OK - Secure Boot enabled
) else (
    echo   FAILED - may not be supported
)

echo.
echo [4/4] Restoring AMD PSP Device...
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\PCI\VEN_1022&DEV_1486&SUBSYS_14861022&REV_00\4&1a8c427&0&0041" /v ConfigFlags /t REG_DWORD /d 0x00000000 /f
if %errorlevel% equ 0 (
    echo   OK - AMD PSP Device enabled
) else (
    echo   FAILED - device path may differ
)

echo.
echo ========================================
echo  NEXT STEPS:
echo ========================================
echo.
echo 1. Reboot computer
echo.
echo 2. Check if VBS/HVCI is restored:
echo    Windows Security > Device Security > Core Isolation
echo    > Memory Integrity = ON
echo.
echo 3. Check if AMD PSP Device is enabled:
echo    Device Manager > Security Devices > AMD PSP Device
echo    > Should be enabled
echo.
pause
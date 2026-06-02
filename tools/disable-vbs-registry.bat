@echo off
echo ========================================
echo  Registry Disable Script for BC-250
echo ========================================
echo.
echo This script disables Memory Integrity and AMD PSP Device
echo via registry to allow PSP access before NBIO firewall locks.
echo.
echo WARNING: This reduces system security!
echo.
pause

echo.
echo [1/4] Disabling Memory Integrity (HVCI)...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity" /v Enabled /t REG_DWORD /d 0 /f
if %errorlevel% equ 0 (
    echo   OK
) else (
    echo   FAILED - run as Administrator
)

echo.
echo [2/4] Disabling VBS...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard" /v EnableVirtualizationBasedSecurity /t REG_DWORD /d 0 /f
if %errorlevel% equ 0 (
    echo   OK
) else (
    echo   FAILED - run as Administrator
)

echo.
echo [3/4] Disabling Secure Boot...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\SecureBoot\State" /v UEFISecureBootEnabled /t REG_DWORD /d 0 /f
if %errorlevel% equ 0 (
    echo   OK
) else (
    echo   FAILED - may not be supported
)

echo.
echo [4/4] Disabling AMD PSP Device...
reg add "HKLM\SYSTEM\CurrentControlSet\Enum\PCI\VEN_1022&DEV_1486&SUBSYS_14861022&REV_00\4&1a8c427&0&0041" /v ConfigFlags /t REG_DWORD /d 0x00000001 /f
if %errorlevel% equ 0 (
    echo   OK
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
echo 2. Install new driver (StartType=0):
echo    Device Manager > Update Driver > Browse to output\
echo.
echo 3. Test PSP initialization:
echo    output\test-psp-init.exe
echo.
echo 4. Check if PSP works
echo.
pause
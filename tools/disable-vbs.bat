@echo off
echo ========================================
echo  VBS/HVCI Disable Script for BC-250
echo ========================================
echo.
echo This script disables Virtualization-Based Security
echo to allow PSP access before NBIO firewall locks.
echo.
echo WARNING: This reduces system security!
echo.
pause

echo.
echo [1/4] Disabling VBS launch policy...
bcdedit /set vsmlaunchpolicy Off
if %errorlevel% equ 0 (
    echo   OK
) else (
    echo   FAILED - run as Administrator
)

echo.
echo [2/4] Disabling Hypervisor...
bcdedit /set hypervisorlaunchtype Off
if %errorlevel% equ 0 (
    echo   OK
) else (
    echo   FAILED - run as Administrator
)

echo.
echo [3/4] Disabling Secure Boot (if possible)...
bcdedit /set {globalsettings} advancedoptions true
if %errorlevel% equ 0 (
    echo   OK
) else (
    echo   FAILED - may not be supported
)

echo.
echo [4/4] Setting platform first initialization...
bcdedit /set platformfirst yes
if %errorlevel% equ 0 (
    echo   OK
) else (
    echo   FAILED - may not be supported
)

echo.
echo ========================================
echo  NEXT STEPS:
echo ========================================
echo.
echo 1. Disable Memory Integrity in Windows:
echo    Windows Security > Device Security > Core Isolation
echo    > Memory Integrity = OFF
echo.
echo 2. Disable AMD PSP Device in Device Manager:
echo    System Devices > AMD PSP Device > Disable
echo.
echo 3. Reboot computer
echo.
echo 4. Install new driver (StartType=0)
echo.
echo 5. Test PSP initialization
echo.
pause
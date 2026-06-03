@echo off
echo ========================================
echo  Complete AMD PSP Disable Script
echo ========================================
echo.
echo This script completely disables AMD PSP in Windows
echo so our driver can access PSP MMIO without interference.
echo.
echo WARNING: This disables security features!
echo.
pause

echo.
echo [1/5] Disabling AMD PSP service in registry...
reg add "HKLM\SYSTEM\CurrentControlSet\Services\amdpsp" /v Start /t REG_DWORD /d 4 /f
if %errorlevel% equ 0 (
    echo   OK - amdpsp service disabled
) else (
    echo   FAILED or service not found
)

echo.
echo [2/5] Disabling AMD PSP Device via registry...
rem Find AMD PSP device and disable it
for /f "tokens=*" %%i in ('reg query "HKLM\SYSTEM\CurrentControlSet\Enum\PCI" /s /f "AMD PSP" /d 2^>nul ^| findstr "HKEY"') do (
    reg add "%%i" /v ConfigFlags /t REG_DWORD /d 0x00000001 /f >nul 2>&1
    echo   Disabled: %%i
)

echo.
echo [3/5] Disabling fTPM in registry...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e96b-e325-11ce-bfc1-08002be10318}\0000" /v UpperFilters /t REG_MULTI_SZ /d "" /f
if %errorlevel% equ 0 (
    echo   OK - fTPM filters removed
) else (
    echo   FAILED or not found
)

echo.
echo [4/5] Disabling AMD Security Processor...
reg add "HKLM\SYSTEM\CurrentControlSet\Services\amdspservice" /v Start /t REG_DWORD /d 4 /f
if %errorlevel% equ 0 (
    echo   OK - amdspservice disabled
) else (
    echo   FAILED or service not found
)

echo.
echo [5/5] Setting platform first initialization...
bcdedit /set hypervisorlaunchtype Off
bcdedit /set nointegritychecks On

echo.
echo ========================================
echo  NEXT STEPS:
echo ========================================
echo.
echo 1. Reboot computer
echo.
echo 2. After reboot, disable AMD PSP in Device Manager:
echo    Device Manager > Security Devices > AMD PSP Device
echo    > Right-click > Disable device
echo.
echo 3. Reboot again
echo.
echo 4. Install our driver (StartType=0):
echo    Device Manager > Update Driver > Browse to output\
echo.
echo 5. Test PSP access:
echo    output\test-psp-init.exe
echo.
echo 6. If PSP works, we can unlock NBIO firewall!
echo.
pause
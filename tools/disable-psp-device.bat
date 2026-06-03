@echo off
echo ========================================
echo  DISABLE AMD PSP DEVICE
echo ========================================
echo.
echo Run as Administrator!
echo.

echo [1] Finding AMD PSP device...
for /f "tokens=*" %%i in ('reg query "HKLM\SYSTEM\CurrentControlSet\Enum\PCI" /s /f "143E" /d 2^>nul ^| findstr "HKEY"') do set PSP_KEY=%%i

if "%PSP_KEY%"=="" (
    echo PSP device not found!
    pause
    exit /b 1
)

echo Found: %PSP_KEY%
echo.

echo [2] Disabling AMD PSP device...
reg add "%PSP_KEY%" /v ConfigFlags /t REG_DWORD /d 0x00000001 /f
if %errorlevel% equ 0 (
    echo   OK - PSP device disabled
) else (
    echo   FAILED - run as Administrator
)

echo.
echo [3] Setting problem code to ignore...
reg add "%PSP_KEY%" /v Problem /t REG_DWORD /d 0 /f
if %errorlevel% equ 0 (
    echo   OK
) else (
    echo   FAILED
)

echo.
echo ========================================
echo  NEXT STEPS:
echo ========================================
echo.
echo 1. Reboot computer
echo 2. Check if PSP device shows as Disabled
echo 3. Check if BC-250 status changed from Degraded
echo 4. Test PSP: output\test-psp-init.exe
echo.
pause
@echo off
echo ========================================
echo  FORCE INSTALL AMD PSP DRIVER
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

echo [2] Creating AMD PSP 3.0 driver registry entry...
rem Set the device to use AMD PSP 3.0 driver
reg add "%PSP_KEY%" /v CompatibleIDs /t REG_MULTI_SZ /d "PCI\VEN_1022&DEV_143E\0PCI\VEN_1022&DEV_143E&REV_00\0PCI\VEN_1022" /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [3] Setting driver class to SecurityDevices...
reg add "%PSP_KEY%" /v ClassGUID /t REG_SZ /d "{D94EE5D8-D189-4994-82D7-7E74F0E1255D}" /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

reg add "%PSP_KEY%" /v Class /t REG_SZ /d "SecurityDevices" /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo [4] Setting problem code to 0 (no problem)...
reg add "%PSP_KEY%" /v Problem /t REG_DWORD /d 0 /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

reg add "%PSP_KEY%" /v ConfigFlags /t REG_DWORD /d 0 /f
if %errorlevel% equ 0 (echo   OK) else (echo   FAILED)

echo.
echo ========================================
echo  DONE!
echo ========================================
echo.
echo Next steps:
echo 1. Open Device Manager
echo 2. Find PCI Encryption/Decryption Controller
echo 3. Right-click -> Update Driver
echo 4. Browse -> Let me pick -> Security Devices
echo 5. Uncheck "Show compatible hardware"
echo 6. Select AMD -> AMD PSP 3.0 Device
echo 7. Install (ignore warning)
echo 8. Reboot
echo.
pause
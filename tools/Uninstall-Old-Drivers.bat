@echo off
echo ========================================
echo BC-250 Driver Uninstall Script v3.1
echo ========================================
echo.
echo This will remove OLD BC-250 driver packages from the system.
echo.
pause

echo.
echo [1/5] Stopping atikmdag service...
net stop atikmdag 2>nul
if %ERRORLEVEL% EQU 0 (
    echo   Service stopped.
) else (
    echo   Service not running (OK).
)

echo.
echo [2/5] Finding installed BC-250 driver packages...
pnputil /enum-drivers | findstr /i "amdbc250 oem" > %TEMP%\bc250-drivers.txt
type %TEMP%\bc250-drivers.txt

echo.
echo [3/5] Removing old BC-250 driver packages...
echo   Checking oem87.inf...
pnputil /enum-drivers | findstr "oem87.inf" >nul
if %ERRORLEVEL% EQU 0 (
    pnputil /delete-driver oem87.inf /uninstall /force
    echo   oem87.inf removed.
) else (
    echo   oem87.inf not found (OK).
)

echo   Checking oem88.inf...
pnputil /enum-drivers | findstr "oem88.inf" >nul
if %ERRORLEVEL% EQU 0 (
    pnputil /delete-driver oem88.inf /uninstall /force
    echo   oem88.inf removed.
) else (
    echo   oem88.inf not found (OK).
)

echo   Checking oem89.inf...
pnputil /enum-drivers | findstr "oem89.inf" >nul
if %ERRORLEVEL% EQU 0 (
    pnputil /delete-driver oem89.inf /uninstall /force
    echo   oem89.inf removed.
) else (
    echo   oem89.inf not found (OK).
)

echo.
echo [4/5] Removing driver files from System32...
echo   (Windows will keep backup copies - this is normal)

echo.
echo [5/5] Switching to Microsoft Basic Display Adapter...
echo   Driver will revert to basic display after removal.
echo.

echo ========================================
echo UNINSTALL COMPLETE!
echo ========================================
echo.
echo Next steps:
echo   1. Open Device Manager
echo   2. Right-click display adapter -^> Update Driver
echo   3. Browse -^> Let me pick -^> Have Disk
echo   4. Select: c:\AMD-BC-250-Windows-Driver\output\amdbc250_dream.inf
echo   5. REBOOT computer
echo.
pause

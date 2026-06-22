@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: reinstall-both-drivers.bat
:: Reinstall both GPU and PSP drivers for AMD BC-250.
:: Run this file as Administrator (Phase 1). It will:
::   1. Uninstall old GPU + PSP drivers
::   2. Reboot
::   3. Auto-run Phase 2 after reboot: install new drivers + reboot again
::
:: IMPORTANT: Save all work before running. The script reboots twice.
:: ============================================================================

title BC-250 Driver Reinstall

:: Phase marker
set "PHASE_FILE=%TEMP%\bc250_reinstall_phase2.flag"
set "LOG_FILE=%TEMP%\bc250_reinstall.log"
set "GPU_INF=C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_dream.inf"
set "PSP_INF=C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\output\PspDriver.inf"
set "GPU_OEM=oem7.inf"
set "PSP_OEM=oem14.inf"

echo ============================================ >> "%LOG_FILE%" 2>&1
echo %date% %time% Starting reinstall script >> "%LOG_FILE%" 2>&1
echo Phase file: %PHASE_FILE% >> "%LOG_FILE%" 2>&1
echo GPU INF: %GPU_INF% >> "%LOG_FILE%" 2>&1
echo PSP INF: %PSP_INF% >> "%LOG_FILE%" 2>&1

:: Check admin rights
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click this file and select "Run as administrator".
    pause
    exit /b 1
)

:: Determine phase
if exist "%PHASE_FILE%" (
    goto Phase2
) else (
    goto Phase1
)

:: ============================================================================
:: PHASE 1: Uninstall old drivers and schedule reboot
:: ============================================================================
:Phase1
echo.
echo ============================================
echo  PHASE 1: Uninstalling old drivers
echo ============================================
echo.

echo Stopping GPU and PSP driver services...
sc stop atikmdag >nul 2>&1
sc stop PspDriver >nul 2>&1

:: Try to delete the driver packages. The exact OEM names may differ.
echo Removing GPU driver package...
pnputil /delete-driver %GPU_OEM% /uninstall /force >> "%LOG_FILE%" 2>&1
if %errorlevel% equ 0 (
    echo   GPU driver removed OK
) else (
    echo   GPU driver remove returned %errorlevel% (may not be installed)
)
echo GPU delete errorlevel=%errorlevel% >> "%LOG_FILE%" 2>&1

echo Removing PSP driver package...
pnputil /delete-driver %PSP_OEM% /uninstall /force >> "%LOG_FILE%" 2>&1
if %errorlevel% equ 0 (
    echo   PSP driver removed OK
) else (
    echo   PSP driver remove returned %errorlevel% (may not be installed)
)
echo PSP delete errorlevel=%errorlevel% >> "%LOG_FILE%" 2>&1

echo.
echo ============================================
echo  Scheduling Phase 2 after reboot
echo ============================================
echo.

echo Creating phase marker...
echo phase2 > "%PHASE_FILE%"

:: Schedule this script to run again after reboot via RunOnce
set "SCRIPT_PATH=%~dpnx0"
echo Registering RunOnce...
echo RunOnce target: "%SCRIPT_PATH%" >> "%LOG_FILE%" 2>&1
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\RunOnce" /v BC250ReinstallPhase2 /t REG_SZ /d "\"%SCRIPT_PATH%\"" /f >> "%LOG_FILE%" 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Failed to register RunOnce. Continuing anyway.
)
echo RunOnce errorlevel=%errorlevel% >> "%LOG_FILE%" 2>&1

echo.
echo ============================================
echo  PHASE 1 complete. Rebooting in 10 seconds.
echo ============================================
echo.
shutdown /r /t 10 /c "BC-250 driver reinstall phase 1"
pause
exit /b 0

:: ============================================================================
:: PHASE 2: Install new drivers after reboot
:: ============================================================================
:Phase2
echo.
echo ============================================
echo  PHASE 2: Installing new drivers
echo ============================================
echo.

:: Clean up RunOnce in case Windows did not remove it
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\RunOnce" /v BC250ReinstallPhase2 /f >> "%LOG_FILE%" 2>&1

:: Wait for PnP subsystem to settle
echo Waiting for PnP to settle...
echo Phase2 start %date% %time% >> "%LOG_FILE%" 2>&1
timeout /t 10 /nobreak >nul

echo Installing GPU driver from %GPU_INF%...
echo pnputil /add-driver "%GPU_INF%" /install /force >> "%LOG_FILE%" 2>&1
pnputil /add-driver "%GPU_INF%" /install /force >> "%LOG_FILE%" 2>&1
if %errorlevel% neq 0 (
    echo ERROR: GPU driver install failed. See %LOG_FILE%
    echo GPU install FAILED errorlevel=%errorlevel% >> "%LOG_FILE%" 2>&1
    goto Phase2Cleanup
)
echo GPU install OK errorlevel=%errorlevel% >> "%LOG_FILE%" 2>&1

echo Installing PSP driver from %PSP_INF%...
echo pnputil /add-driver "%PSP_INF%" /install /force >> "%LOG_FILE%" 2>&1
pnputil /add-driver "%PSP_INF%" /install /force >> "%LOG_FILE%" 2>&1
if %errorlevel% neq 0 (
    echo ERROR: PSP driver install failed. See %LOG_FILE%
    echo PSP install FAILED errorlevel=%errorlevel% >> "%LOG_FILE%" 2>&1
    goto Phase2Cleanup
)
echo PSP install OK errorlevel=%errorlevel% >> "%LOG_FILE%" 2>&1

echo.
echo ============================================
echo  PHASE 2 complete. Rebooting in 10 seconds.
echo ============================================
echo.

:Phase2Cleanup
:: Delete phase marker so next run starts Phase 1 again
del /f "%PHASE_FILE%" >nul 2>&1

echo Phase2 cleanup %date% %time% >> "%LOG_FILE%" 2>&1

:: Final reboot
shutdown /r /t 10 /c "BC-250 driver reinstall phase 2 complete"
pause
exit /b 0

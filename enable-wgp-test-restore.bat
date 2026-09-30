@echo off
REM Restore the safe hardware-init cap after the WGP unlock test.
REM HwInitMaxStep = 1 stops the sequence right after the fence setup, which is
REM the configuration this machine has been running in all along.
REM Run as Administrator.

setlocal
set K=HKLM\SYSTEM\CurrentControlSet\Services\atikmdag

net session >nul 2>&1
if errorlevel 1 (
  echo ERROR: not running as Administrator.
  exit /b 1
)

reg add "%K%" /v HwInitMaxStep /t REG_DWORD /d 1 /f >nul
if errorlevel 1 (echo ERROR: registry write failed. & exit /b 1)

echo Restored. Current values:
for %%A in (HwInitMaxStep HwInitGart HwInitVm HwInitGfxRing HwInitSdmaRing HwInitFirmware HwInitMemCtrl HwInitExtended) do (
  reg query "%K%" /v %%A
)
echo.
echo Reboot to return to the short init sequence.
endlocal

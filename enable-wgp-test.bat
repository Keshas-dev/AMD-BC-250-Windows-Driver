@echo off
REM BC-250 WGP unlock test - run ONLY the WGP step of the extended init.
REM Run as Administrator.
REM
REM HwInitExtended = 1 selects DreamV3HwInitializeExtended, which is the default
REM path and the one that actually contains the WGP step. Its per-step guard is
REM HwInitMaxStep, and the WGP step is number 12. Setting the cap to exactly 12
REM means: steps 0b, 1, 2, 3, 4, 9, 10, 6 and 12 run, and it stops before 13
REM (RLC), 7 (GFX ring), 8 (SDMA ring) and 11 (display).
REM
REM That isolation is deliberate. The WGP step writes SPI_PG = 0x1F across four
REM banks, which is the unlock itself. The steps that follow it - RLC, ring
REM base setup and display - are the ones historically associated with 0x1A
REM MEMORY_MANAGEMENT crashes and black screens on this board, and none of them
REM are needed to find out whether SPI_PG accepts the write.
REM
REM A cap of 12 also avoids the failure ordering problem: with the cap at 0 the
REM run stops inside the ring or display step before the WGP step runs, which is
REM what made an earlier attempt look as though the unlock had never been tried.

setlocal
set K=HKLM\SYSTEM\CurrentControlSet\Services\atikmdag

net session >nul 2>&1
if errorlevel 1 (
  echo ERROR: not running as Administrator.
  echo Right-click this file and choose "Run as administrator".
  exit /b 1
)

echo Isolating the WGP init step (cap = 12)...
reg add "%K%" /v HwInitMaxStep  /t REG_DWORD /d 12 /f >nul
reg add "%K%" /v HwInitGart     /t REG_DWORD /d 0 /f >nul
reg add "%K%" /v HwInitVm       /t REG_DWORD /d 0 /f >nul
reg add "%K%" /v HwInitGfxRing  /t REG_DWORD /d 0 /f >nul
reg add "%K%" /v HwInitSdmaRing /t REG_DWORD /d 0 /f >nul
reg add "%K%" /v HwInitFirmware /t REG_DWORD /d 1 /f >nul
reg add "%K%" /v HwInitMemCtrl  /t REG_DWORD /d 1 /f >nul
reg add "%K%" /v HwInitExtended /t REG_DWORD /d 1 /f >nul
if errorlevel 1 (echo ERROR: registry write failed. & exit /b 1)

echo.
echo Current values:
for %%A in (HwInitMaxStep HwInitGart HwInitVm HwInitGfxRing HwInitSdmaRing HwInitFirmware HwInitMemCtrl HwInitExtended) do (
  reg query "%K%" /v %%A
)

echo.
echo === NEXT STEPS ===
echo 1. Reboot.
echo 2. Run:  C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\full-init-test.exe
echo 3. Read the result:
echo      reg query "%K%" /v Step_HwInit
echo    Step_HwInit = 12 means the WGP step ran.
echo 4. Then run:  output\smu-unlock-staged.exe bankprobe
echo    SPI_PG = 0x1F means the unlock landed. All zeros means it did not.
echo 5. Restore afterwards:  enable-wgp-test-restore.bat
echo.
echo The WGP step writes SPI_PG = 0x1F, which enables compute units. Community
echo reports say enabling a defective WGP can hard-lock the GPU, so a black
echo screen is a real possibility. Recover with a reboot; if that does not come
echo back, power-cycle the box.
endlocal

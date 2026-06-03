@echo off
echo ========================================
echo  CHECK AND FIX BOOT CONFIGURATION
echo ========================================
echo.
echo Run as Administrator!
echo.

echo [1] Current boot configuration:
bcdedit /enum
echo.

echo [2] Setting boot menu policy...
bcdedit /set {current} bootmenupolicy Standard
echo.

echo [3] Disabling integrity checks...
bcdedit /set nointegritychecks on
echo.

echo [4] Disabling hypervisor...
bcdedit /set hypervisorlaunchtype off
echo.

echo [5] Enabling test signing...
bcdedit /set testsigning on
echo.

echo [6] Checking if changes applied:
bcdedit /enum
echo.

echo ========================================
echo  DONE! Reboot and test.
echo ========================================
pause
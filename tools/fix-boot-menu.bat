@echo off
echo ========================================
echo  FIX BOOT MENU AND DRIVER ISSUES
echo ========================================
echo.
echo Run as Administrator!
echo.

echo [1] Removing F8 boot menu...
bcdedit /set bootmenupolicy Standard
echo.

echo [2] Disabling integrity checks for drivers...
bcdedit /set nointegritychecks on
echo.

echo [3] Disabling hypervisor...
bcdedit /set hypervisorlaunchtype off
echo.

echo [4] Setting test signing on...
bcdedit /set testsigning on
echo.

echo ========================================
echo  DONE! Reboot computer.
echo ========================================
pause
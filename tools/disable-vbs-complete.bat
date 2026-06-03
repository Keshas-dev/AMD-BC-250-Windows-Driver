@echo off
echo ========================================
echo  DISABLE VBS/HVCI COMPLETELY
echo ========================================
echo.
echo Run as Administrator!
echo.

echo [1] Disabling VBS launch policy...
bcdedit /set vsmlaunchpolicy Off
echo.

echo [2] Disabling hypervisor...
bcdedit /set hypervisorlaunchtype off
echo.

echo [3] Disabling integrity checks...
bcdedit /set nointegritychecks on
echo.

echo [4] Enabling test signing...
bcdedit /set testsigning on
echo.

echo [5] Setting boot menu policy...
bcdedit /set {current} bootmenupolicy Standard
echo.

echo ========================================
echo  DONE! Reboot computer.
echo ========================================
pause
@echo off
echo ========================================
echo  COMPLETE VBS/HYPERVISOR DISABLE
echo ========================================
echo.
echo Run as Administrator!
echo.

echo [1] Disabling VBS via registry...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard" /v EnableVirtualizationBasedSecurity /t REG_DWORD /d 0 /f
echo.

echo [2] Disabling LSA credentials guard...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Lsa" /v LsaCfgFlags /t REG_DWORD /d 0 /f
echo.

echo [3] Disabling hypervisor...
bcdedit /set hypervisorlaunchtype off
echo.

echo [4] Disabling integrity checks...
bcdedit /set nointegritychecks on
echo.

echo [5] Enabling test signing...
bcdedit /set testsigning on
echo.

echo ========================================
echo  NEXT STEPS:
echo ========================================
echo.
echo 1. Open "Turn Windows features on or off"
echo 2. Uncheck: Hyper-V
echo 3. Uncheck: Virtual Machine Platform
echo 4. Uncheck: Windows Hypervisor Platform
echo 5. Click OK and reboot
echo.
echo After reboot:
echo - Install new driver from output\
echo - Check if Degraded status is fixed
echo - Test PSP: output\test-psp-init.exe
echo.
pause
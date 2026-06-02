@echo off
cd /d "%~dp0"
echo Stopping old driver...
sc.exe stop atikmdag 2>nul
echo Copying new driver to System32...
copy /Y output\atikmdag.sys C:\Windows\System32\drivers\atikmdag.sys
if errorlevel 1 (
    echo Copy failed - trying alternate method
    takeown /f C:\Windows\System32\drivers\atikmdag.sys >nul 2>&1
    icacls C:\Windows\System32\drivers\atikmdag.sys /grant Administrators:F >nul 2>&1
    copy /Y output\atikmdag.sys C:\Windows\System32\drivers\atikmdag.sys
)
echo Starting new driver...
sc.exe start atikmdag
echo.
echo === Running tests ===
echo.
output\test-pci-config-ioctl.exe
echo.
output\test-ecam-direct.exe
echo.
echo === Done ===
pause

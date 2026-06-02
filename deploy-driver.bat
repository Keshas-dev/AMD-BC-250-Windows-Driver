@echo off
echo Deploying new atikmdag.sys...
copy /Y "%~dp0output\atikmdag.sys" "%SystemRoot%\System32\drivers\atikmdag.sys"
if %ERRORLEVEL% NEQ 0 (
    echo FAILED to copy driver. Run as Administrator!
    pause
    exit /b 1
)
echo Starting driver...
sc create atikmdag type= kernel start= demand binPath= "%SystemRoot%\System32\drivers\atikmdag.sys" DisplayName= "AMD Radeon BC-250 Graphics (Dream drivers)"
sc start atikmdag
echo.
echo Running test...
"%~dp0output\test-pci-config-ioctl.exe"
echo.
pause

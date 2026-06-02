@echo off
echo ============================================
echo BC-250 Driver Deploy Script (Admin)
echo ============================================
echo.

set "SRC=C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\atikmdag.sys"
set "DST=C:\Windows\System32\drivers\atikmdag.sys"
set "LOG=C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\deploy.log"

echo Deploy started: %DATE% %TIME% > "%LOG%"

rem Step 1: Try to stop the driver service
echo [1] Stopping atikmdag service...
net stop atikmdag >> "%LOG%" 2>&1
echo   Result: %errorlevel% >> "%LOG%"

rem Step 2: Try to delete the service (for clean reinstall)
echo [2] Deleting atikmdag service entry...
sc delete atikmdag >> "%LOG%" 2>&1
echo   Result: %errorlevel% >> "%LOG%"

rem Step 3: Backup old driver
echo [3] Backing up old driver...
if exist "%DST%" (
    copy "%DST%" "%DST%.bak" /Y >> "%LOG%" 2>&1
    echo   Backup: %DST%.bak >> "%LOG%"
)

rem Step 4: Copy new driver
echo [4] Copying new driver...
copy "%SRC%" "%DST%" /Y >> "%LOG%" 2>&1
echo   Result: %errorlevel% >> "%LOG%"
if %errorlevel% neq 0 (
    echo   ERROR: Copy failed! >> "%LOG%"
    echo   Check "%LOG%" for details
    goto :Done
)

rem Step 5: Verify copy
echo [5] Verifying...
dir "%DST%" >> "%LOG%" 2>&1
echo   Size: >> "%LOG%"

rem Step 6: Disable the service to prevent PnP reload of broken driver
echo [6] Disabling atikmdag service...
sc config atikmdag start= disabled >> "%LOG%" 2>&1
echo   Result: %errorlevel% >> "%LOG%"

rem Step 7: Also disable via registry to be sure
echo [7] Setting registry to disabled...
reg add "HKLM\SYSTEM\CurrentControlSet\Services\atikmdag" /v Start /t REG_DWORD /d 4 /f >> "%LOG%" 2>&1
echo   Result: %errorlevel% >> "%LOG%"

echo.
echo Deploy completed: %DATE% %TIME% >> "%LOG%"
echo.
echo ============================================
echo RESULT: Driver deployed. Service disabled.
echo REBOOT required to unload broken driver
echo and load the fixed one.
echo ============================================
echo.
echo Check %LOG% for details

:Done
echo.
pause

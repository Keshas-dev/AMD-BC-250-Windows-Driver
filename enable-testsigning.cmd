@echo off
bcdedit /set testsigning on
echo Test signing: %errorlevel%
bcdedit /enum {current} | findstr /i "testsigning"
echo.
echo Please REBOOT now:
echo   shutdown /r /t 5

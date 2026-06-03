@echo off
echo ========================================
echo  Enable Unsigned Drivers
echo ========================================
echo.
echo This command allows unsigned drivers to load.
echo.
pause

bcdedit /set nointegritychecks on
if %errorlevel% equ 0 (
    echo.
    echo SUCCESS! Unsigned drivers now allowed.
    echo.
    echo Reboot required for changes to take effect.
) else (
    echo.
    echo FAILED! Run as Administrator.
)

pause
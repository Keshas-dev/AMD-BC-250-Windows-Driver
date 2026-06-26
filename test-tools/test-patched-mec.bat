@echo off
REM test-patched-mec.bat — Test patched MEC firmware with KIQ ring
REM Run from Admin prompt with GPU driver installed

setlocal
set FW_DIR=..\firmware
set MEC_ORIG=%FW_DIR%\cyan_skillfish2_mec.bin
set MEC_PATCHED=%FW_DIR%\cyan_skillfish2_mec_patched.bin
set MEC_BACKUP=%FW_DIR%\cyan_skillfish2_mec.bin.orig

if not exist %MEC_PATCHED% (
    echo ERROR: Patched firmware not found at %MEC_PATCHED%
    exit /b 1
)

echo === Patched MEC Firmware Test ===
echo.

REM Backup original firmware
echo Backing up original firmware...
copy /Y %MEC_ORIG% %MEC_BACKUP% >nul
echo Backed up to %MEC_BACKUP%

REM Copy patched firmware in place of original
echo Installing patched firmware...
copy /Y %MEC_PATCHED% %MEC_ORIG% >nul
echo Done.

REM Run the test
echo.
echo Running patched-mec-test.exe...
patched-mec-test.exe
set TEST_RESULT=%ERRORLEVEL%

REM Restore original firmware
echo.
echo Restoring original firmware...
copy /Y %MEC_BACKUP% %MEC_ORIG% >nul
del %MEC_BACKUP%
echo Restored.

echo.
if %TEST_RESULT% equ 0 (
    echo TEST PASSED: KIQ ring works with patched MEC firmware!
) else (
    echo TEST FAILED: KIQ ring still blocked by KIQ_SIZE=0
)
exit /b %TEST_RESULT%

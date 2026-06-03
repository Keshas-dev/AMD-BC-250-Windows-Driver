@echo off
echo ========================================
echo  FIX BOOT WITH CORRECT SYNTAX
echo ========================================
echo.
echo Run as Administrator!
echo.

echo [1] Setting bootmenupolicy for {current}...
bcdedit /set {current} bootmenupolicy Standard
echo.

echo [2] Setting nointegritychecks for {current}...
bcdedit /set {current} nointegritychecks on
echo.

echo [3] Setting testsigning for {current}...
bcdedit /set {current} testsigning on
echo.

echo [4] Setting hypervisorlaunchtype for {current}...
bcdedit /set {current} hypervisorlaunchtype off
echo.

echo [5] Removing displaymessageoverride...
bcdedit /deletevalue {current} displaymessageoverride
echo.

echo [6] Disabling recovery...
bcdedit /set {current} recoveryenabled No
echo.

echo ========================================
echo  VERIFYING SETTINGS:
echo ========================================
bcdedit /enum {current}
echo.
pause
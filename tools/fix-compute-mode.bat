@echo off
echo ========================================
echo  BC-250 COMPUTE MODE REGISTRY FIX
echo ========================================
echo.
echo Run as Administrator!
echo.

echo [1] Finding BC-250 registry key...
for /f "tokens=*" %%i in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}" /s /f "AMD BC-250" /d 2^>nul ^| findstr "HKEY"') do set BC250_KEY=%%i

if "%BC250_KEY%"=="" (
    echo BC-250 not found in registry!
    echo Trying alternative search...
    for /f "tokens=*" %%i in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}" /s /f "Dream" /d 2^>nul ^| findstr "HKEY"') do set BC250_KEY=%%i
)

if "%BC250_KEY%"=="" (
    echo ERROR: Cannot find BC-250 in registry!
    pause
    exit /b 1
)

echo Found: %BC250_KEY%
echo.

echo [2] Setting FeatureScore to CF...
reg add "%BC250_KEY%" /v FeatureScore /t REG_DWORD /d 0xCF /f
echo.

echo [3] Setting AdapterType to 2 (compute accelerator)...
reg add "%BC250_KEY%" /v AdapterType /t REG_DWORD /d 2 /f
echo.

echo [4] Setting DalForceAllDisplaysConnected...
reg add "%BC250_KEY%" /v DalForceAllDisplaysConnected /t REG_DWORD /d 1 /f
echo.

echo [5] Setting PP_ThermalControllerType to 0...
reg add "%BC250_KEY%" /v PP_ThermalControllerType /t REG_DWORD /d 0 /f
echo.

echo [6] Setting PPLib_EnableShadowPstate...
reg add "%BC250_KEY%" /v PPLib_EnableShadowPstate /t REG_DWORD /d 1 /f
echo.

echo [7] Setting Kmd_EnableGuestMemoryValidation to 0...
reg add "%BC250_KEY%" /v Kmd_EnableGuestMemoryValidation /t REG_DWORD /d 0 /f
echo.

echo ========================================
echo  REGISTRY CHANGES APPLIED!
echo ========================================
echo.
echo NEXT STEPS:
echo 1. Open Device Manager
echo 2. Right-click BC-250 -> Disable device
echo 3. Wait 5 seconds
echo 4. Right-click BC-250 -> Enable device
echo 5. Check if status changed from Degraded
echo 6. Test PSP: output\test-psp-init.exe
echo.
pause
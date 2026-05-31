@echo off
setlocal enabledelayedexpansion
echo ====================================================
echo   AMD BC-250 Dream Drivers v4.3 - Full Installer
echo ====================================================
echo.

set "PROJECT_DIR=%~dp0.."
set "OUTPUT_DIR=%PROJECT_DIR%\output"
set "DXVK_DIR=%PROJECT_DIR%\third-party\dxvk-2.7.1"
set "NVAPI_DIR=%PROJECT_DIR%\third-party"
set "SYS32=C:\Windows\System32"
set "DRIVERS=C:\Windows\System32\drivers"

rem --- Check admin privileges ---
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] This installer requires Administrator privileges!
    echo         Right-click and select "Run as administrator"
    echo.
    pause
    exit /b 1
)
echo [OK] Running as Administrator
echo.

rem --- Check build output exists ---
if not exist "%OUTPUT_DIR%\atikmdag.sys" (
    echo [ERROR] KMD not found: %OUTPUT_DIR%\atikmdag.sys
    echo         Run build.bat first!
    pause
    exit /b 1
)
if not exist "%OUTPUT_DIR%\amdbc250umd64.dll" (
    echo [ERROR] UMD not found: %OUTPUT_DIR%\amdbc250umd64.dll
    echo         Run build.bat first!
    pause
    exit /b 1
)
if not exist "%OUTPUT_DIR%\amdbc250vulkan.dll" (
    echo [ERROR] Vulkan ICD not found: %OUTPUT_DIR%\amdbc250vulkan.dll
    echo         Run build.bat first!
    pause
    exit /b 1
)
echo [OK] Build output found
echo.

rem =============================================
rem  STEP 1: Install Kernel-Mode Driver
rem =============================================
echo [Step 1/7] Installing Kernel-Mode Driver (atikmdag.sys)...

rem --- Stop old driver if running ---
sc stop AMDBC250DreamV43 >nul 2>&1
sc delete AMDBC250DreamV43 >nul 2>&1

rem --- Copy KMD ---
copy /Y "%OUTPUT_DIR%\atikmdag.sys" "%DRIVERS%\atikmdag.sys" >nul
if %errorlevel% neq 0 (
    echo [ERROR] Failed to copy KMD to %DRIVERS%
    pause
    exit /b 1
)
echo   Copied: %DRIVERS%\atikmdag.sys

rem --- Install driver service ---
reg add "HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43" /v "Type" /t REG_DWORD /d 1 /f >nul
reg add "HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43" /v "Start" /t REG_DWORD /d 1 /f >nul
reg add "HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43" /v "ErrorControl" /t REG_DWORD /d 1 /f >nul
reg add "HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43" /v "ImagePath" /t REG_EXPAND_SZ /d "system32\drivers\atikmdag.sys" /f >nul
reg add "HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43" /v "DisplayName" /t REG_SZ /d "AMD BC-250 Dream Drivers v4.3" /f >nul
echo   Service installed: AMDBC250DreamV43
echo.

rem =============================================
rem  STEP 2: Copy PSP Firmware
rem =============================================
echo [Step 2/7] Copying PSP firmware files...
set "FW_DIR=%PROJECT_DIR%\third-party\firmware"
set "FW_DEST=%DRIVERS%\amdgpu"
if not exist "%FW_DEST%" mkdir "%FW_DEST%"
if exist "%FW_DIR%\navi10_sos.bin" (
    copy /Y "%FW_DIR%\navi10_sos.bin" "%FW_DEST%\navi10_sos.bin" >nul
    echo   Copied: navi10_sos.bin
) else (
    echo   [WARN] navi10_sos.bin not found in third-party\firmware\
)
if exist "%FW_DIR%\navi10_asd.bin" (
    copy /Y "%FW_DIR%\navi10_asd.bin" "%FW_DEST%\navi10_asd.bin" >nul
    echo   Copied: navi10_asd.bin
) else (
    echo   [WARN] navi10_asd.bin not found in third-party\firmware\
)
if exist "%FW_DIR%\navi10_ta.bin" (
    copy /Y "%FW_DIR%\navi10_ta.bin" "%FW_DEST%\navi10_ta.bin" >nul
    echo   Copied: navi10_ta.bin
) else (
    echo   [WARN] navi10_ta.bin not found in third-party\firmware\
)
echo.

rem =============================================
rem  STEP 3: Install User-Mode Driver
rem =============================================
echo [Step 3/7] Installing User-Mode Driver (amdbc250umd64.dll)...
copy /Y "%OUTPUT_DIR%\amdbc250umd64.dll" "%SYS32%\amdbc250umd64.dll" >nul
echo   Copied: %SYS32%\amdbc250umd64.dll
echo.

rem =============================================
rem  STEP 3: Install Vulkan ICD
rem =============================================
echo [Step 4/7] Installing Vulkan ICD...
copy /Y "%OUTPUT_DIR%\amdbc250vulkan.dll" "%SYS32%\amdbc250vulkan.dll" >nul
echo   Copied: %SYS32%\amdbc250vulkan.dll

rem --- Create Vulkan ICD JSON manifest ---
if not exist "%OUTPUT_DIR%\amdbc250_icd.json" (
    echo   Creating Vulkan ICD manifest...
    (
        echo {
        echo     "file_format_version" : "1.0.0",
        echo     "ICD" : {
        echo         "library_path" : "C:\\Windows\\System32\\amdbc250vulkan.dll",
        echo         "api_version" : "1.3.0"
        echo     }
        echo }
    ) > "%OUTPUT_DIR%\amdbc250_icd.json"
)
copy /Y "%OUTPUT_DIR%\amdbc250_icd.json" "%SYS32%\amdbc250_icd.json" >nul

rem --- Register Vulkan ICD ---
reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "%SYS32%\amdbc250_icd.json" /t REG_DWORD /d 0 /f >nul
echo   Vulkan ICD registered
echo.

rem =============================================
rem  STEP 4: Install DXVK (D3D9/10/11 -> Vulkan)
rem =============================================
echo [Step 5/8] Installing DXVK 2.7.1 (D3D9/10/11 -> Vulkan translation)...

if not exist "%DXVK_DIR%\x64\d3d11.dll" (
    echo [ERROR] DXVK not found in %DXVK_DIR%
    echo         Download from: https://github.com/doitsujin/dxvk/releases
    pause
    exit /b 1
)

rem --- System-wide DXVK (replaces built-in D3D DLLs) ---
copy /Y "%DXVK_DIR%\x64\d3d9.dll" "%SYS32%\d3d9.dll" >nul
copy /Y "%DXVK_DIR%\x64\d3d11.dll" "%SYS32%\d3d11.dll" >nul
copy /Y "%DXVK_DIR%\x64\d3d10core.dll" "%SYS32%\d3d10core.dll" >nul
copy /Y "%DXVK_DIR%\x64\dxgi.dll" "%SYS32%\dxgi.dll" >nul
echo   64-bit DLLs installed to %SYS32%

rem --- 32-bit DLLs for WoW64 ---
copy /Y "%DXVK_DIR%\x32\d3d9.dll" "C:\Windows\SysWOW64\d3d9.dll" >nul
copy /Y "%DXVK_DIR%\x32\d3d11.dll" "C:\Windows\SysWOW64\d3d11.dll" >nul
copy /Y "%DXVK_DIR%\x32\d3d10core.dll" "C:\Windows\SysWOW64\d3d10core.dll" >nul
copy /Y "%DXVK_DIR%\x32\dxgi.dll" "C:\Windows\SysWOW64\dxgi.dll" >nul
echo   32-bit DLLs installed to SysWOW64
echo.

rem =============================================
rem  STEP 5: Install DXVK-NVAPI
rem =============================================
echo [Step 6/8] Installing DXVK-NVAPI 0.9.2...

if not exist "%NVAPI_DIR%\x64\nvapi64.dll" (
    echo [ERROR] DXVK-NVAPI not found in %NVAPI_DIR%
    pause
    exit /b 1
)

copy /Y "%NVAPI_DIR%\x64\nvapi64.dll" "%SYS32%\nvapi64.dll" >nul
copy /Y "%NVAPI_DIR%\x64\nvofapi64.dll" "%SYS32%\nvofapi64.dll" >nul
copy /Y "%NVAPI_DIR%\x32\nvapi.dll" "C:\Windows\SysWOW64\nvapi.dll" >nul
echo   NVAPI DLLs installed
echo.

rem =============================================
rem  STEP 6: Install INF (for Device Manager)
rem =============================================
echo [Step 7/8] Installing driver INF...
if exist "%OUTPUT_DIR%\amdbc250_dream.inf" (
    "%SYS32%\pnputil.exe" /add-driver "%OUTPUT_DIR%\amdbc250_dream.inf" /install 2>nul
    echo   INF installed via pnputil
) else (
    echo   [WARN] INF not found, skipping
)
echo.

rem =============================================
rem  STEP 7: Enable 40 CU unlock (optional)
rem =============================================
echo [Step 8/8] Checking 40 CU unlock setting...
reg query "HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43" /v Enable40CU >nul 2>&1
if %errorlevel% equ 0 (
    echo   40 CU unlock: ENABLED
) else (
    echo   40 CU unlock: not set (run with /40cu flag to enable)
)

rem --- Parse command line flags ---
if /I "%1"=="/40cu" (
    reg add "HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43" /v Enable40CU /t REG_DWORD /d 1 /f >nul
    echo   40 CU unlock: ENABLED (reboot required)
)
echo.

rem =============================================
rem  SUMMARY
rem =============================================
echo ====================================================
echo   Installation Complete!
echo ====================================================
echo.
echo   Installed components:
echo     [KMD]   atikmdag.sys          - Kernel driver
echo     [UMD]   amdbc250umd64.dll     - User-mode driver (D3D9)
echo     [VK]    amdbc250vulkan.dll    - Vulkan ICD
echo     [DXVK]  d3d9/d3d11/dxgi.dll   - D3D -> Vulkan translation
echo     [NVAPI] nvapi64.dll           - NVAPI implementation
echo.
echo   Next steps:
echo     1. Reboot to load kernel driver
echo     2. Run: test-tools\test-gpu-ioctls.exe
echo     3. Run: test-tools\test-vulkan-icd.exe
echo     4. Try a game with DXVK!
echo.
echo   To enable 40 CU unlock:
echo     install.bat /40cu
echo.
pause

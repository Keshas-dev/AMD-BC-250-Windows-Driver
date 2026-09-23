@echo off
echo AMD BC-250 Dream Drivers Build + Sign Script v4.3
echo ==================================================

set "PROJECT_DIR=%~dp0"
set "SRC_DIR=%PROJECT_DIR%src"
set "INC_DIR=%PROJECT_DIR%inc"
set "OUTPUT_DIR=%PROJECT_DIR%output"
set "CERT_FILE=%PROJECT_DIR%testcert.pfx"
set "CERT_NAME=AMD-BC250-Signer"
rem --- Only AMD-BC250-Signer with this SHA1 is installed in Root+TrustedPublisher.
rem     Multiple same-name certs exist in My store; /a auto-select is ambiguous. ---
set "CERT_SHA1=34AFF96C57E9ADE68B23B4828859CF9B7F4EF442"

rem --- Detect Visual Studio on D:, E:, or C: drive ---
set "VSWHERE="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    echo Found Visual Studio 2022 BuildTools on C: drive
    goto :SetupEnv
)
if exist "D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSWHERE=D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    echo Found Visual Studio 2022 Professional on D: drive
    goto :SetupEnv
)
if exist "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSWHERE=D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    echo Found Visual Studio 2022 Community on D: drive
    goto :SetupEnv
)
if exist "E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSWHERE=E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    echo Found Visual Studio 2022 Community on E: drive
    goto :SetupEnv
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSWHERE=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    echo Found Visual Studio 2022 Community on C: drive
    goto :SetupEnv
)
if exist "E:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSWHERE=E:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    echo Found Visual Studio 2022 Professional on E: drive
    goto :SetupEnv
)
if exist "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSWHERE=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    echo Found Visual Studio 2022 Community on F: drive
    goto :SetupEnv
)
if exist "F:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSWHERE=F:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    echo Found Visual Studio 2022 Professional on F: drive
    goto :SetupEnv
)
echo ERROR: Visual Studio 2022 not found
exit /b 1

:SetupEnv
echo Setting up build environment...
call "%VSWHERE%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to setup VS build environment
    exit /b 1
)

rem --- Detect Windows Kit ---
set "WDK_ROOT="
if exist "D:\Program Files (x86)\Windows Kits\10\Include" (
    set "WDK_ROOT=D:\Program Files (x86)\Windows Kits\10"
) else if exist "E:\Program Files (x86)\Windows Kits\10\Include" (
    set "WDK_ROOT=E:\Program Files (x86)\Windows Kits\10"
) else if exist "F:\Program Files (x86)\Windows Kits\10\Include" (
    set "WDK_ROOT=F:\Program Files (x86)\Windows Kits\10"
) else if exist "C:\Program Files (x86)\Windows Kits\10\Include" (
    set "WDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
) else (
    echo ERROR: Windows Kit not found
    exit /b 1
)

set "WDK_VERSION="
for /f "delims=" %%V in ('dir /b /ad "%WDK_ROOT%\Include" ^| sort /r') do (
    if exist "%WDK_ROOT%\Include\%%V\km\ntddk.h" (
        set "WDK_VERSION=%%V"
        goto :FoundWDK
    )
)
echo ERROR: No kernel headers found
exit /b 1

:FoundWDK
echo Using Windows Kit version %WDK_VERSION%

rem --- Locate signing tools ---
set "SIGNTOOLS="
set "INF2CAT="
if exist "%WDK_ROOT%\bin\%WDK_VERSION%\x64\signtool.exe" (
    set "SIGNTOOLS=%WDK_ROOT%\bin\%WDK_VERSION%\x64"
)
if exist "%WDK_ROOT%\bin\%WDK_VERSION%\x86\signtool.exe" (
    if "%SIGNTOOLS%"=="" set "SIGNTOOLS=%WDK_ROOT%\bin\%WDK_VERSION%\x86"
)
if exist "%WDK_ROOT%\bin\%WDK_VERSION%\x86\Inf2Cat.exe" (
    set "INF2CAT=%WDK_ROOT%\bin\%WDK_VERSION%\x86\Inf2Cat.exe"
)
if exist "%WDK_ROOT%\bin\%WDK_VERSION%\x64\Inf2Cat.exe" (
    set "INF2CAT=%WDK_ROOT%\bin\%WDK_VERSION%\x64\Inf2Cat.exe"
)
rem --- Fallback: try signtool from PATH (VS/SDK adds it) ---
if "%SIGNTOOLS%"=="" (
    where signtool.exe >nul 2>&1 && set "SIGNTOOLS=." && echo Found signtool in PATH
)
if "%SIGNTOOLS%"=="" (
    echo WARNING: signtool.exe not found - will skip signing
)

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if exist "%PROJECT_DIR%*.obj" del /q "%PROJECT_DIR%*.obj" 2>nul

echo.
echo ==========================================
echo  PRE-BUILD VALIDATION
echo ==========================================
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0prebuild-check.ps1"
if errorlevel 1 (
    echo PRE-BUILD CHECK FAILED - Aborting build
    pause
    exit /b 1
)
echo.

echo ==========================================
echo  BUILDING KMD (Kernel-Mode Driver)
echo ==========================================
echo.

cl.exe /c /kernel /W3 /Zi /Od /DAMD64 /D_AMD64_ /DAMDBC250_DREAM_V3 ^
  /I"%WDK_ROOT%\Include\%WDK_VERSION%\km" ^
  /I"%WDK_ROOT%\Include\%WDK_VERSION%\km\crt" ^
  /I"%WDK_ROOT%\Include\%WDK_VERSION%\shared" ^
  /I"%INC_DIR%" ^
  /I"%SRC_DIR%\kmd" ^
  "%SRC_DIR%\kmd\amdbc250_dream_kmd.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_hw_init.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_hw_init_extended.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_power.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_vm.c" ^
  "%SRC_DIR%\kmd\amdbc250_psp.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_fw_load.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_psp_fw_load.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_golden.c" ^
   "%SRC_DIR%\kmd\amdbc250_dream_hdp.c" ^
   "%SRC_DIR%\kmd\amdbc250_dream_rlc.c" ^
   "%SRC_DIR%\kmd\amdbc250_dream_vbios.c" ^
   "%SRC_DIR%\kmd\amdbc250_dream_kmd_ddi_stubs.c"

if errorlevel 1 (
    echo KMD compilation FAILED!
    pause
    exit /b 1
)

echo Linking KMD...
link.exe /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
  /OUT:"%OUTPUT_DIR%\atikmdag.sys" ^
  amdbc250_dream_kmd.obj amdbc250_dream_hw_init.obj amdbc250_dream_hw_init_extended.obj amdbc250_dream_power.obj amdbc250_dream_vm.obj amdbc250_psp.obj amdbc250_dream_fw_load.obj amdbc250_dream_psp_fw_load.obj amdbc250_dream_golden.obj amdbc250_dream_hdp.obj amdbc250_dream_rlc.obj amdbc250_dream_vbios.obj amdbc250_dream_kmd_ddi_stubs.obj ^
  ntoskrnl.lib wdm.lib win32k.lib ntstrsafe.lib BufferOverflowK.lib hal.lib displib.lib ^
  /LIBPATH:"%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64"

if errorlevel 1 (
    echo KMD linking FAILED!
    pause
    exit /b 1
)

rem echo.
rem echo ==========================================
rem echo  BUILDING PSP (PSP Driver — DISABLED, integrated into dream driver)
rem echo ==========================================
rem echo.
rem
rem cl.exe /c /kernel /W3 /Zi /Od /DAMD64 /D_AMD64_ /GS- ^
rem   /I"%WDK_ROOT%\Include\%WDK_VERSION%\km" ^
rem   /I"%WDK_ROOT%\Include\%WDK_VERSION%\km\crt" ^
rem   /I"%WDK_ROOT%\Include\%WDK_VERSION%\shared" ^
rem   /I"%INC_DIR%" ^
rem   /I"%SRC_DIR%\kmd" ^
rem   "%SRC_DIR%\kmd\amdbc250_psp_driver.c" ^
rem   "%SRC_DIR%\kmd\amdbc250_psp.c"
rem
rem if errorlevel 1 (
rem     echo PSP compilation FAILED!
rem     pause
rem     exit /b 1
rem )
rem
rem echo Linking PSP...
rem link.exe /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /NODEFAULTLIB ^
rem   /OUT:"%OUTPUT_DIR%\amdbc250_psp.sys" ^
rem   amdbc250_psp_driver.obj amdbc250_psp.obj ^
rem   ntoskrnl.lib hal.lib wdm.lib ^
rem   /LIBPATH:"%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64"
rem
rem if errorlevel 1 (
rem     echo PSP linking FAILED!
rem     pause
rem     exit /b 1
rem )

echo.
echo ==========================================
echo  BUILDING UMD (User-Mode Driver)
echo ==========================================
echo.

cl.exe /c /TP /D_AMD64_ /DWIN64 /DAMDBC250_UMD /W3 /Zi /O2 ^
  /I"%WDK_ROOT%\Include\%WDK_VERSION%\um" ^
  /I"%WDK_ROOT%\Include\%WDK_VERSION%\shared" ^
  /I"%WDK_ROOT%\Include\%WDK_VERSION%\ucrt" ^
  /I"%INC_DIR%" ^
  "%SRC_DIR%\umd\amdbc250_umd_v46.c"

if errorlevel 1 (
    echo UMD compilation FAILED!
    pause
    exit /b 1
)

echo Linking UMD...
link.exe /DLL /DEF:"%SRC_DIR%\umd\amdbc250_umd.def" /OUT:"%OUTPUT_DIR%\amdbc250umd64.dll" amdbc250_umd_v46.obj ^
  d3d12.lib dxgi.lib dxguid.lib user32.lib ^
  /LIBPATH:"%WDK_ROOT%\Lib\%WDK_VERSION%\um\x64" ^
  /LIBPATH:"%WDK_ROOT%\Lib\%WDK_VERSION%\ucrt\x64"

if errorlevel 1 (
    echo UMD linking FAILED!
    pause
    exit /b 1
)

echo.
echo Copying INF file...
copy "%PROJECT_DIR%\inf\amdbc250_dream.inf" "%OUTPUT_DIR%\" >nul

echo Copying firmware files...
if not exist "%OUTPUT_DIR%\firmware" mkdir "%OUTPUT_DIR%\firmware"
copy "%PROJECT_DIR%\firmware\cyan_skillfish2_*.bin" "%OUTPUT_DIR%\firmware\" >nul 2>&1
copy "%PROJECT_DIR%\firmware\navi12_sdma*.bin" "%OUTPUT_DIR%\firmware\" >nul 2>&1
echo   Firmware files copied

echo.
echo ==========================================
echo  SIGNING DRIVERS
echo ==========================================
rem --- Sign kernel driver FIRST (most important) ---
:SignKmd
echo Signing atikmdag.sys...
"%SIGNTOOLS%\signtool.exe" sign /fd SHA256 /sha1 %CERT_SHA1% /v ^
  "%OUTPUT_DIR%\atikmdag.sys" > "%OUTPUT_DIR%\sign-kmd.log" 2>&1
if errorlevel 1 (
    type "%OUTPUT_DIR%\sign-kmd.log"
    echo FATAL: KMD signing FAILED!
    echo.
    echo Try: Run build.bat as Administrator, or sign manually:
    echo   signtool sign /fd SHA256 /a /s My /n AMD-BC250-Signer output\atikmdag.sys
    pause
    exit /b 1
) else (
    echo   KMD signed OK
)

rem --- Verify KMD signature ---
"%SIGNTOOLS%\signtool.exe" verify /pa /v "%OUTPUT_DIR%\atikmdag.sys" > "%OUTPUT_DIR%\verify-kmd.log" 2>&1
echo KMD signature verification: OK

rem --- Generate catalog file (REQUIRED: stale/missing CAT = package treated as Unsigned) ---
if "%INF2CAT%"=="" (
    echo WARNING: Inf2Cat not found - catalog NOT generated, package will be Unsigned!
    goto :AfterCat
)
rem Remove stale CAT so a failed Inf2Cat cannot leave a mismatched one behind.
if exist "%OUTPUT_DIR%\amdbc250_dream.cat" del /q "%OUTPUT_DIR%\amdbc250_dream.cat"
rem --- Use a CLEAN temp dir for Inf2Cat (output\ has stale INFs in subdirs that cause 22.9.4 errors) ---
set "INF2CAT_DIR=%TEMP%\bc250_inf2cat"
if exist "%INF2CAT_DIR%" rmdir /s /q "%INF2CAT_DIR%"
mkdir "%INF2CAT_DIR%" 2>nul
mkdir "%INF2CAT_DIR%\firmware" 2>nul
copy "%OUTPUT_DIR%\atikmdag.sys" "%INF2CAT_DIR%\" >nul 2>&1
copy "%OUTPUT_DIR%\amdbc250_dream.inf" "%INF2CAT_DIR%\" >nul 2>&1
copy "%OUTPUT_DIR%\amdbc250umd64.dll" "%INF2CAT_DIR%\" >nul 2>&1
copy "%OUTPUT_DIR%\firmware\*.bin" "%INF2CAT_DIR%\firmware\" >nul 2>&1
echo Generating catalog file (clean temp)...
"%INF2CAT%" /driver:"%INF2CAT_DIR%" /os:10_x64 > "%OUTPUT_DIR%\inf2cat.log" 2>&1
if errorlevel 1 (
    echo FATAL: Inf2Cat FAILED - see output\inf2cat.log
    type "%OUTPUT_DIR%\inf2cat.log"
    rmdir /s /q "%INF2CAT_DIR%" 2>nul
    pause
    exit /b 1
)
copy "%INF2CAT_DIR%\amdbc250_dream.cat" "%OUTPUT_DIR%\" >nul 2>&1
rmdir /s /q "%INF2CAT_DIR%" 2>nul
echo   Catalog generated OK
:AfterCat

rem --- Sign catalog (REQUIRED: unsigned CAT = package treated as Unsigned) ---
if exist "%OUTPUT_DIR%\amdbc250_dream.cat" (
    "%SIGNTOOLS%\signtool.exe" sign /fd SHA256 /sha1 %CERT_SHA1% ^
      "%OUTPUT_DIR%\amdbc250_dream.cat" >nul 2>&1
    if errorlevel 1 (
        echo FATAL: CAT signing FAILED - package would be unsigned.
        pause
        exit /b 1
    )
    echo   Catalog signed OK
)

rem --- Sign UMD ---
echo Signing amdbc250umd64.dll...
"%SIGNTOOLS%\signtool.exe" sign /fd SHA256 /sha1 %CERT_SHA1% ^
  "%OUTPUT_DIR%\amdbc250umd64.dll" >nul 2>&1
if errorlevel 1 (
    echo WARNING: UMD signing failed (non-fatal)
) else (
    echo   UMD signed OK
)

echo.
echo ==========================================
echo  BUILD COMPLETED!
echo ==========================================
echo.
echo  Output: %OUTPUT_DIR%
echo    atikmdag.sys       - GPU Kernel driver (signed)
echo    amdbc250umd64.dll  - User driver
echo    amdbc250_dream.inf
echo.
echo ==========================================
echo  AUTO-INSTALL (run as Admin)
echo ==========================================
rem --- Export cert to .cer and import to Trusted Root ---
certutil -exportPFX -p bc250sign "%CERT_FILE%" "%TEMP%\bc250_signer.cer" >nul 2>&1
certutil -addstore Root "%TEMP%\bc250_signer.cer" >nul 2>&1
certutil -addstore TrustedPublisher "%TEMP%\bc250_signer.cer" >nul 2>&1
del "%TEMP%\bc250_signer.cer" 2>nul
echo   Certificate added to Trusted Root + Trusted Publisher

rem --- Install via pnputil ---
echo Installing driver...
pnputil /add-driver "%OUTPUT_DIR%\amdbc250_dream.inf" /install 2>&1
if errorlevel 1 (
    echo WARNING: pnputil failed. Install manually:
    echo   Device Manager -^> Uninstall AMD Radeon BC-250 (check Delete driver)
    echo   Reboot
    echo   Device Manager -^> Update driver -^> Browse -^> %OUTPUT_DIR%
    echo   Reboot
) else (
    echo   Driver installed OK - reboot to activate
)
echo.
pause

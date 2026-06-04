@echo off
echo AMD BC-250 Dream Drivers Build + Sign Script v4.3
echo ==================================================

set "PROJECT_DIR=%~dp0"
set "SRC_DIR=%PROJECT_DIR%src"
set "INC_DIR=%PROJECT_DIR%inc"
set "OUTPUT_DIR=%PROJECT_DIR%output"
set "CERT_FILE=%PROJECT_DIR%testcert.pfx"
set "CERT_NAME=AMD-BC250-Signer"

rem --- Detect Visual Studio on E: or C: drive ---
set "VSWHERE="
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
if exist "E:\Program Files (x86)\Windows Kits\10\Include" (
    set "WDK_ROOT=E:\Program Files (x86)\Windows Kits\10"
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
if "%SIGNTOOLS%"=="" (
    echo WARNING: signtool.exe not found - will skip signing
)

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if exist "%PROJECT_DIR%*.obj" del /q "%PROJECT_DIR%*.obj" 2>nul

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
  "%SRC_DIR%\kmd\amdbc250_dream_power.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_vm.c" ^
  "%SRC_DIR%\kmd\amdbc250_psp_v11.c"

if errorlevel 1 (
    echo KMD compilation FAILED!
    pause
    exit /b 1
)

echo Linking KMD...
link.exe /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
  /OUT:"%OUTPUT_DIR%\atikmdag.sys" ^
  amdbc250_dream_kmd.obj amdbc250_dream_hw_init.obj amdbc250_dream_power.obj amdbc250_dream_vm.obj amdbc250_psp_v11.obj ^
  ntoskrnl.lib wdm.lib win32k.lib ntstrsafe.lib BufferOverflowK.lib hal.lib ^
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
rem   "%SRC_DIR%\kmd\amdbc250_psp_v11.c"
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
rem   amdbc250_psp_driver.obj amdbc250_psp_v11.obj ^
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

echo.
echo ==========================================
echo  SIGNING DRIVERS
echo ==========================================
echo.

if "%SIGNTOOLS%"=="" (
    echo WARNING: Skipping signing - signtool not found
    goto :SkipSigning
)

rem --- Create certificate if not exists ---
if not exist "%CERT_FILE%" (
    echo Creating self-signed test certificate...
    "%SIGNTOOLS%\makecert.exe" -r -pe -n "CN=%CERT_NAME%" ^
      -ss My -sr CurrentUser ^
      -sky signature -a sha256 -m 60 ^
      "%CERT_FILE%"
    if errorlevel 1 (
        echo WARNING: Certificate creation failed - skipping signing
        goto :SkipSigning
    )
    echo Certificate created.
    
    rem --- Trust the certificate (add to Root store) ---
    echo Adding certificate to Root trust store...
    certutil -addstore -user Root "%CERT_FILE%" >nul 2>&1
    certutil -addstore -user TrustedPublisher "%CERT_FILE%" >nul 2>&1
    echo   Certificate trusted.
)

rem --- Generate catalog file ---
if "%INF2CAT%"=="" (
    echo WARNING: Inf2Cat not found - skipping catalog generation
    goto :SignFiles
)
echo Generating catalog file...
"%INF2CAT%" /driver:"%OUTPUT_DIR%" /os:10_x64 /verbose
if errorlevel 1 (
    echo WARNING: inf2cat failed - signing individual files only
    goto :SignFiles
)

rem --- Sign catalog ---
echo Signing amdbc250_dream.cat...
"%SIGNTOOLS%\signtool.exe" sign /fd SHA256 /a /s My /n "%CERT_NAME%" ^
  "%OUTPUT_DIR%\amdbc250_dream.cat"
if errorlevel 1 (
    echo WARNING: Catalog signing failed
) else (
    echo   OK
)

:SignFiles
rem --- Sign KMD ---
echo Signing atikmdag.sys...
"%SIGNTOOLS%\signtool.exe" sign /fd SHA256 /a /s My /n "%CERT_NAME%" ^
  "%OUTPUT_DIR%\atikmdag.sys"
if errorlevel 1 (
    echo WARNING: KMD signing failed
) else (
    echo   OK
)

rem --- PSP driver no longer built separately (integrated into atikmdag.sys) ---

rem --- Sign UMD ---
echo Signing amdbc250umd64.dll...
"%SIGNTOOLS%\signtool.exe" sign /fd SHA256 /a /s My /n "%CERT_NAME%" ^
  "%OUTPUT_DIR%\amdbc250umd64.dll"
if errorlevel 1 (
    echo WARNING: UMD signing failed
) else (
    echo   OK
)

rem --- Verify ---
echo.
echo Verifying signatures...
"%SIGNTOOLS%\signtool.exe" verify /pa "%OUTPUT_DIR%\atikmdag.sys" 2>nul
"%SIGNTOOLS%\signtool.exe" verify /pa "%OUTPUT_DIR%\amdbc250umd64.dll" 2>nul
if exist "%OUTPUT_DIR%\amdbc250_dream.cat" (
    "%SIGNTOOLS%\signtool.exe" verify /pa "%OUTPUT_DIR%\amdbc250_dream.cat" 2>nul
)

:SkipSigning

echo.
echo ==========================================
echo  BUILD COMPLETED!
echo ==========================================
echo.
echo  Output: %OUTPUT_DIR%
echo    atikmdag.sys       - GPU Kernel driver (WDM, PSP integrated)
echo    amdbc250umd64.dll  - User driver
echo    amdbc250_dream.inf
echo.
echo  NOTES:
echo    - PSP logic is compiled INTO atikmdag.sys (GPU BAR5 mapping, MP0 discovery)
echo    - Separate amdbc250_psp.sys driver is DISABLED (no longer needed)
echo    - NBIO unlock attempted automatically during StartDevice
echo.
pause

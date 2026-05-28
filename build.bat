@echo off
echo AMD BC-250 Dream Drivers Build Script v4.3
echo ==========================================

set "PROJECT_DIR=%~dp0"
set "SRC_DIR=%PROJECT_DIR%src"
set "INC_DIR=%PROJECT_DIR%inc"
set "OUTPUT_DIR=%PROJECT_DIR%output"

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

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if exist "%PROJECT_DIR%*.obj" del /q "%PROJECT_DIR%*.obj" 2>nul

echo.
echo Building KMD (Kernel-Mode Driver)...
echo.

cl.exe /c /kernel /W3 /Zi /Od /DAMD64 /D_AMD64_ /DAMDBC250_DREAM_V3 ^
  /I"%WDK_ROOT%\Include\%WDK_VERSION%\km" ^
  /I"%WDK_ROOT%\Include\%WDK_VERSION%\km\crt" ^
  /I"%WDK_ROOT%\Include\%WDK_VERSION%\shared" ^
  /I"%INC_DIR%" ^
  "%SRC_DIR%\kmd\amdbc250_dream_v3_kmd.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_v3_hw_init.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_v3_power.c" ^
  "%SRC_DIR%\kmd\amdbc250_dream_v3_vm.c"

if errorlevel 1 (
    echo KMD compilation FAILED!
    pause
    exit /b 1
)

echo Linking KMD...
link.exe /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
  /OUT:"%OUTPUT_DIR%\atikmdag.sys" ^
  amdbc250_dream_v3_kmd.obj amdbc250_dream_v3_hw_init.obj amdbc250_dream_v3_power.obj amdbc250_dream_v3_vm.obj ^
  ntoskrnl.lib wdm.lib win32k.lib ntstrsafe.lib BufferOverflowK.lib ^
  /LIBPATH:"%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64"

if errorlevel 1 (
    echo KMD linking FAILED!
    pause
    exit /b 1
)

echo.
echo Building UMD (User-Mode Driver)...
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
link.exe /DLL /OUT:"%OUTPUT_DIR%\amdbc250umd64.dll" amdbc250_umd_v46.obj ^
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
copy "%PROJECT_DIR%\inf\amdbc250_dream_v3.inf" "%OUTPUT_DIR%\"

echo.
echo ==========================================
echo BUILD COMPLETED SUCCESSFULLY!
echo Output: %OUTPUT_DIR%
echo ==========================================
echo.
pause

@echo off
setlocal enabledelayedexpansion

echo ==========================================
echo  Compiling PSP PM4 Submit Test Tool
echo ==========================================

set "PROJECT_DIR=C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main"
set "OUTPUT_DIR=%PROJECT_DIR%\output"
set "TOOL_SRC=%PROJECT_DIR%\test-tools\psp-pm4-submit-test.c"
set "TOOL_OUT=%OUTPUT_DIR%\psp-pm4-submit-test.exe"

:: Detect VS2022
set "VSWHERE="
for %%D in (C D E F G H) do (
    if exist "%%D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        set "VSWHERE=%%D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
        goto :SetupEnv
    )
    if exist "%%D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VSWHERE=%%D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
        goto :SetupEnv
    )
)
echo ERROR: VS2022 not found
exit /b 1

:SetupEnv
call "%VSWHERE%"

set "SDK_INC=D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt"
set "SDK_LIB=D:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64"
set "UM_INC=D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um"
set "UM_LIB=D:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
set "SHARED_INC=D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"

cl.exe /nologo /Fe:"%TOOL_OUT%" ^
    /I"%SDK_INC%" /I"%UM_INC%" /I"%SHARED_INC%" ^
    "%TOOL_SRC%" ^
    /link /LIBPATH:"%SDK_LIB%" /LIBPATH:"%UM_LIB%" ^
    kernel32.lib user32.lib advapi32.lib

if errorlevel 1 (
    echo COMPILATION FAILED!
    pause
    exit /b 1
)

echo.
echo Compiled: %TOOL_OUT%
echo Run: %TOOL_OUT%
pause

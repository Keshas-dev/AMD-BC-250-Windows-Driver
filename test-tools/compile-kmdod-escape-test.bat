@echo off
REM compile-kmdod-escape-test.bat - Build the KMDOD Escape user-mode test tool
setlocal

set "TOOL=kmdod-escape-test"
set "OUT=output\%TOOL%.exe"

call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set "WDK_ROOT=F:\Program Files (x86)\Windows Kits\10"
set "WDK_VER=10.0.26100.0"
set "KIT_SHARED=%WDK_ROOT%\Include\%WDK_VER%\shared"
set "KIT_UM=%WDK_ROOT%\Include\%WDK_VER%\um"
set "KIT_UCRT=%WDK_ROOT%\Include\%WDK_VER%\ucrt"
set "KIT_LIB=%WDK_ROOT%\Lib\%WDK_VER%\um\x64"
set "MSVC_INC=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\include"
set "MSVC_LIB=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64"

set "INCLUDE=%MSVC_INC%;%KIT_UCRT%;%KIT_SHARED%;%KIT_UM%;%INCLUDE%"
set "LIB=%MSVC_LIB%;%KIT_UCRT%\x64;%KIT_LIB%;%LIB%"

cl /nologo /O2 /W3 /Fe%OUT% "%~dp0%TOOL%.c" /link /subsystem:console "%KIT_LIB%\gdi32.lib" "%KIT_LIB%\user32.lib" "%KIT_LIB%\advapi32.lib" "%KIT_LIB%\kernel32.lib" "%KIT_LIB%\ntdll.lib" "%KIT_LIB%\uuid.lib"

if %errorlevel% neq 0 (
    echo COMPILE FAILED
    exit /b 1
)
echo COMPILE OK
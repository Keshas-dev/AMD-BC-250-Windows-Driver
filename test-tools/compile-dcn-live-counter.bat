@echo off
rem compile-dcn-live-counter.bat — build dcn-live-counter.exe
setlocal

set "VSDIR=F:\Program Files\Microsoft Visual Studio\2022\Community"
set "MSVCVER=14.44.35207"
set "SDKDIR=F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
set "LIBDIR=F:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"

call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "INCLUDE=%VSDIR%\VC\Tools\MSVC\%MSVCVER%\include;%SDKDIR%\ucrt;%SDKDIR%\shared;%SDKDIR%\um;%SDKDIR%\winrt;%INCLUDE%"
set "LIB=%VSDIR%\VC\Tools\MSVC\%MSVCVER%\lib\x64;%LIBDIR%\ucrt\x64;%LIBDIR%\um\x64;%LIB%"

set SRCDIR=%~dp0
set OUTDIR=%SRCDIR%..\output
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

cl /nologo /W3 /O1 /MT /Fe"%OUTDIR%\dcn-live-counter.exe" ^
    "%SRCDIR%dcn-live-counter.c" /link user32.lib kernel32.lib
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo OK: %OUTDIR%\dcn-live-counter.exe

@echo off
setlocal
:: compile-pa-v1-probe.bat — CPU-PSP pa_v1 platform mailbox register probe
set SRCDIR=%~dp0
set ODIR=%~dp0..\output
if not exist "%ODIR%" mkdir "%ODIR%"
set VCVARS=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat
set SDK=F:\Program Files (x86)\Windows Kits\10
set SDKVER=10.0.26100.0
call "%VCVARS%" x64 >nul
cl /nologo /O2 /utf-8 /MD /W3 /wd4100 /wd4101 /wd4189 /wd4996 ^
  /I"%SDK%\Include\%SDKVER%\um" ^
  /I"%SDK%\Include\%SDKVER%\shared" ^
  /I"%SDK%\Include\%SDKVER%\ucrt" ^
  /Fe"%ODIR%\pa-v1-probe.exe" ^
  "%SRCDIR%pa-v1-probe.c" ^
  /link /libpath:"%SDK%\Lib\%SDKVER%\um\x64" /libpath:"%SDK%\Lib\%SDKVER%\ucrt\x64" ^
  kernel32.lib
if errorlevel 1 (echo BUILD FAILED & exit /b 1) else (echo OK: output\pa-v1-probe.exe)

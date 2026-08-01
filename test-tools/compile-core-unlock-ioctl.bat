@echo off
setlocal
set SRCDIR=%~dp0
set ODIR=%~dp0..\output
if not exist "%ODIR%" mkdir "%ODIR%"
set VCVARS=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat
if not exist "%VCVARS%" set "VCVARS=E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set SDK=F:\Program Files (x86)\Windows Kits\10
if not exist "%SDK%" set "SDK=E:\Program Files (x86)\Windows Kits\10"
set SDKVER=10.0.26100.0
call "%VCVARS%" x64
cl /nologo /O2 /utf-8 /MD /W3 /wd4100 /wd4101 /wd4189 /wd4996 ^
  /I"%SRCDIR%..\inc" ^
  /I"%SDK%\Include\%SDKVER%\um" ^
  /I"%SDK%\Include\%SDKVER%\shared" ^
  /I"%SDK%\Include\%SDKVER%\ucrt" ^
  /Fe"%ODIR%\core-unlock-ioctl.exe" ^
  "%SRCDIR%core-unlock-ioctl.c" ^
  /link /libpath:"%SDK%\Lib\%SDKVER%\um\x64" /libpath:"%SDK%\Lib\%SDKVER%\ucrt\x64" ^
  kernel32.lib user32.lib
if errorlevel 1 (echo BUILD FAILED & exit /b 1) else (echo OK: output\core-unlock-ioctl.exe)

@echo off
setlocal enabledelayedexpansion
call "D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "EXTRA_INC=D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared;D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt;D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\km\crt"
set "EXTRA_LIB=D:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;D:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
set "INCLUDE=!EXTRA_INC!;!INCLUDE!"
set "LIB=!EXTRA_LIB!;!LIB!"
cl.exe /nologo /W3 /O2 "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\hqd-test.c" /Fe:C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\hqd-test.exe /link /LIBPATH:"D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.44.35207\lib\x64" /LIBPATH:"D:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" /LIBPATH:"D:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" user32.lib
if %errorlevel% neq 0 (echo BUILD FAILED & exit /b %errorlevel%)
echo BUILD OK
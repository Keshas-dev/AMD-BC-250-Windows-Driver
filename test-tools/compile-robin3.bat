@echo off
setlocal enabledelayedexpansion
call "F:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "EXTRA_INC=F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared;F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt"
set "EXTRA_LIB=F:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;F:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
set "INCLUDE=!EXTRA_INC!;!INCLUDE!"
set "LIB=!EXTRA_LIB!;!LIB!"
cl.exe /nologo /W3 /O2 "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\robin3-metrics-test.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\robin3-metrics-test.exe" user32.lib
if %errorlevel% neq 0 (echo BUILD FAILED & exit /b %errorlevel%)
echo BUILD OK
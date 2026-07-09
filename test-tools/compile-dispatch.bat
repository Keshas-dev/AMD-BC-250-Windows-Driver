@echo off
call "E:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "EXTRA_INC=E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt"
set "EXTRA_LIB=E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
set "INCLUDE=%EXTRA_INC%;%INCLUDE%"
set "LIB=%EXTRA_LIB%;%LIB%"
cl.exe /nologo /O2 "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\dispatch-seg1-test.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\dispatch-seg1-test.exe"
if %errorlevel% neq 0 (echo BUILD FAILED) else (echo BUILD OK)
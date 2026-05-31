@echo off
call "E:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "SDK_INC=E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt"
set "SDK_LIB=E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
set "INCLUDE=%SDK_INC%;%INCLUDE%"
set "LIB=%SDK_LIB%;%LIB%"
cl.exe /nologo /W3 /O2 "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-gpu-hw-init.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-gpu-hw-init.exe" /link /LIBPATH:"E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" /LIBPATH:"E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" advapi32.lib kernel32.lib

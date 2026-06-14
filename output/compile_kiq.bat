@echo off
call "E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set INCLUDE=E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt;%INCLUDE%
set LIB=E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64;%LIB%
cl /nologo /Fe:C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\kiq-hqd-init.exe C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\kiq-hqd-init.c /I C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc /I C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd /link user32.lib

@echo off
setlocal enabledelayedexpansion
call "D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "KIT=D:\Program Files (x86)\Windows Kits\10"
set "KIT_VER=10.0.26100.0"
set "INCLUDE=!KIT!\Include\!KIT_VER!\ucrt;!KIT!\Include\!KIT_VER!\shared;!KIT!\Include\!KIT_VER!\um;!KIT!\Include\!KIT_VER!\winrt;!INCLUDE!"
set "LIB=!KIT!\Lib\!KIT_VER!\ucrt\x64;!KIT!\Lib\!KIT_VER!\um\x64;!LIB!"
cl.exe /nologo /W3 /O2 "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\pt-base-test.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\pt-base-test.exe" user32.lib

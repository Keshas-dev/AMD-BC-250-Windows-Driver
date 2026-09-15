@echo off
setlocal enabledelayedexpansion
call "F:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
if %errorlevel% neq 0 call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "VULKAN_SDK=F:\VulkanSDK\1.4.341.1"
set "EXTRA_INC=%VULKAN_SDK%\Include;F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared;F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um"
set "EXTRA_LIB=%VULKAN_SDK%\Lib;F:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;F:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
set "INCLUDE=!EXTRA_INC!;!INCLUDE!"
set "LIB=!EXTRA_LIB!;!LIB!"
cl.exe /nologo /W3 /O2 "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\vk-submit-trace-test.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\vk-submit-trace-test.exe" vulkan-1.lib user32.lib advapi32.lib
if %errorlevel% neq 0 (echo BUILD FAILED & exit /b %errorlevel%)
echo BUILD OK

@echo off
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set MSVC=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set WDK=F:\Program Files (x86)\Windows Kits\10
set KIT=10.0.26100.0
set VULKAN=F:\VulkanSDK\1.4.341.1
set INCLUDE=%MSVC%\include;%VULKAN%\Include;%WDK%\Include\%KIT%\ucrt;%WDK%\Include\%KIT%\shared;%WDK%\Include\%KIT%\um;%WDK%\Include\%KIT%\km;%WDK%\Include\%KIT%\km\crt;%INCLUDE%
set LIB=%MSVC%\lib\x64;%VULKAN%\Lib;%WDK%\Lib\%KIT%\ucrt\x64;%WDK%\Lib\%KIT%\um\x64;%LIB%
cd /d C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools
cl /nologo /O2 /W3 /LD /FeC:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdradv64.dll radv-icd-stub.c /link /subsystem:console /def:radv-icd-stub.def /OUT:amdradv64.dll
if %errorlevel% neq 0 (echo BUILD FAILED) else (echo BUILD OK)

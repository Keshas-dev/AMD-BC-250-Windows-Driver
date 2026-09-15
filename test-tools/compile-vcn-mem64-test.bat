@echo off
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "MSVC=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
set "WDK=F:\Program Files (x86)\Windows Kits\10"
set "KIT=10.0.26100.0"
set "INCLUDE=%MSVC%\include;%WDK%\Include\%KIT%\ucrt;%WDK%\Include\%KIT%\shared;%WDK%\Include\%KIT%\um;%WDK%\Include\%KIT%\km;%WDK%\Include\%KIT%\km\crt;C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc;%INCLUDE%"
set "LIB=%MSVC%\lib\x64;%WDK%\Lib\%KIT%\ucrt\x64;%WDK%\Lib\%KIT%\um\x64;%LIB%"
cd /d C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools
cl /nologo /O2 /utf-8 /W3 /FeC:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\vcn-mem64-test.exe vcn-mem64-test.c /link /subsystem:console
if %errorlevel% neq 0 (echo BUILD FAILED) else (echo BUILD OK)

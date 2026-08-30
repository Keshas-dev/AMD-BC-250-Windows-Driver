@echo off
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "MSVC=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
set "WDK=F:\Program Files (x86)\Windows Kits\10"
set "KIT=10.0.26100.0"
set "INCLUDE=%MSVC%\include;%WDK%\Include\%KIT%\ucrt;%WDK%\Include\%KIT%\shared;%WDK%\Include\%KIT%\um;%WDK%\Include\%KIT%\km;%INCLUDE%"
set "LIB=%MSVC%\lib\x64;%WDK%\Lib\%KIT%\ucrt\x64;%WDK%\Lib\%KIT%\um\x64"
cd /d C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools
cl /nologo /O2 /utf-8 /W3 /FeC:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\stale-verdict-recheck.exe stale-verdict-recheck.c /link /subsystem:console
if %errorlevel% equ 0 (echo Build OK) else (echo BUILD FAILED)

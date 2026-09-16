@echo off
setlocal
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "WINVER=10.0.26100.0"
set "WDK=F:\Program Files (x86)\Windows Kits\10"
set "VCTools=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
set "INCLUDE=%VCTools%\include;%WDK%\Include\%WINVER%\ucrt;%WDK%\Include\%WINVER%\shared;%WDK%\Include\%WINVER%\um"
set "LIB=%VCTools%\lib\x64;%WDK%\Lib\%WINVER%\ucrt\x64;%WDK%\Lib\%WINVER%\um\x64"
cd /d "%~dp0"
cl /nologo /O2 /utf-8 /W3 /Fe..\output\radv-backend-test.exe radv-backend-test.c /link /subsystem:console
if %errorlevel% equ 0 (echo BUILD OK) else (echo BUILD FAILED)
endlocal

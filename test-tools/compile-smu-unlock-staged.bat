@echo off
rem Compile the staged SMU secure-access diagnostics tool.
rem Mirrors compile-smu-table-transfer-test.bat, which is known to work with the
rem F: drive layout of the VS2022 Community install and the 10.0.26100.0 WDK.
setlocal
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "MSVC=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
set "WDK=F:\Program Files (x86)\Windows Kits\10"
set "KIT=10.0.26100.0"
set "REPO=C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main"
set "INCLUDE=%MSVC%\include;%WDK%\Include\%KIT%\ucrt;%WDK%\Include\%KIT%\shared;%WDK%\Include\%KIT%\um;%WDK%\Include\%KIT%\km;%WDK%\Include\%KIT%\km\crt;%REPO%\inc;%INCLUDE%"
set "LIB=%MSVC%\lib\x64;%WDK%\Lib\%KIT%\ucrt\x64;%WDK%\Lib\%KIT%\um\x64;%LIB%"
cd /d %REPO%\test-tools
cl /nologo /O2 /utf-8 /W3 /Fe%REPO%\output\smu-unlock-staged.exe smu-unlock-staged.c /link /subsystem:console
if %errorlevel% neq 0 (echo BUILD FAILED) else (echo BUILD OK)
endlocal

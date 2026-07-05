@echo off
call "E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set "WDK=E:\Program Files (x86)\Windows Kits\10"
set "WINVER=10.0.26100.0"
set "INCLUDE=%WDK%\Include\%WINVER%\ucrt;%WDK%\Include\%WINVER%\shared;%WDK%\Include\%WINVER%\um;%INCLUDE%"
set "LIB=%WDK%\Lib\%WINVER%\ucrt\x64;%WDK%\Lib\%WINVER%\um\x64;%LIB%"
cl /nologo /Zi /O2 /W3 "test-tools\smu-direct-test.c" /Fe:"output\smu-direct-test.exe"
if %errorlevel% neq 0 pause

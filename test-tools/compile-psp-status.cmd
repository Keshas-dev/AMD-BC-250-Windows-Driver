@echo off
call "E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set KIT=E:\Program Files (x86)\Windows Kits\10
set SDK=10.0.26100.0
set INCLUDE=%KIT%\Include\%SDK%\ucrt;%KIT%\Include\%SDK%\shared;%KIT%\Include\%SDK%\um;%KIT%\Include\%SDK%\winrt;%INCLUDE%
set LIB=%KIT%\Lib\%SDK%\ucrt\x64;%KIT%\Lib\%SDK%\um\x64;%LIB%
cl /nologo /W3 /O2 "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\psp-status-test.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-status-test.exe"
if %errorlevel% neq 0 (echo BUILD FAILED & exit /b %errorlevel%)
echo BUILD OK

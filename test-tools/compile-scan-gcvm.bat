@echo off
setlocal
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "KIT=D:\Program Files (x86)\Windows Kits\10"
set "SDK=10.0.26100.0"
set "INCLUDE=%KIT%\Include\%SDK%\ucrt;%KIT%\Include\%SDK%\shared;%KIT%\Include\%SDK%\um;%KIT%\Include\%SDK%\winrt;%INCLUDE%"
set "LIB=%KIT%\Lib\%SDK%\ucrt\x64;%KIT%\Lib\%SDK%\um\x64;%LIB%"
cl.exe /nologo /W3 /O2 "%~dp0scan-gcvm.c" /Fe"%~dp0..\output\scan-gcvm.exe" user32.lib
if %errorlevel% neq 0 (echo BUILD FAILED & exit /b %errorlevel%)
echo BUILD OK

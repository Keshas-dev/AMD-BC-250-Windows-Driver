@echo off
setlocal enabledelayedexpansion
call "E:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "SDK=E:\Program Files (x86)\Windows Kits\10"
set "VER=10.0.26100.0"
set "INCLUDE=%SDK%\Include\%VER%\ucrt;%SDK%\Include\%VER%\shared;%SDK%\Include\%VER%\um;%SDK%\Include\%VER%\winrt;%INCLUDE%"
set "LIB=%SDK%\Lib\%VER%\ucrt\x64;%SDK%\Lib\%VER%\um\x64;%LIB%"
cl.exe /nologo /O1 /W3 /utf-8 "%~dp0cp-write-test.c" /Fe"%~dp0..\output\cp-write-test.exe" /link /subsystem:console
if %ERRORLEVEL% NEQ 0 (echo BUILD FAILED) else (echo BUILD OK)

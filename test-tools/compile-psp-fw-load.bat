@echo off
setlocal enabledelayedexpansion

set "VS_DIR=F:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VS_DIR%" set "VS_DIR=F:\Program Files\Microsoft Visual Studio\2022\Professional"
if not exist "%VS_DIR%" set "VS_DIR=E:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VS_DIR%" set "VS_DIR=E:\Program Files\Microsoft Visual Studio\2022\Professional"

if not exist "%VS_DIR%" echo ERROR: VS2022 not found & exit /b 1

call "%VS_DIR%\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1

set "SDK_ROOT=F:\Program Files (x86)\Windows Kits\10"
if not exist "!SDK_ROOT!" set "SDK_ROOT=E:\Program Files (x86)\Windows Kits\10"

set "SDK_VER=10.0.26100.0"
if not exist "!SDK_ROOT!\Include\!SDK_VER!" set "SDK_VER=10.0.22621.0"
if not exist "!SDK_ROOT!\Include\!SDK_VER!" set "SDK_VER=10.0.19041.0"

set "EXTRA_INC=!SDK_ROOT!\Include\!SDK_VER!\ucrt;!SDK_ROOT!\Include\!SDK_VER!\shared;!SDK_ROOT!\Include\!SDK_VER!\um;!SDK_ROOT!\Include\!SDK_VER!\winrt"
set "EXTRA_LIB=!SDK_ROOT!\Lib\!SDK_VER!\ucrt\x64;!SDK_ROOT!\Lib\!SDK_VER!\um\x64"

set "INCLUDE=!EXTRA_INC!;!INCLUDE!"
set "LIB=!EXTRA_LIB!;!LIB!"

set "PROJECT_DIR=%~dp0.."
set "OUTPUT_DIR=%PROJECT_DIR%\output"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

cl.exe /nologo /O2 /MT /W3 /Fe"%OUTPUT_DIR%\psp-fw-load.exe" ^
  /I"%PROJECT_DIR%\inc" ^
  "%PROJECT_DIR%\test-tools\psp-fw-load.c" ^
  /link user32.lib

if errorlevel 1 (echo FAILED & exit /b 1) else (echo OK: output\psp-fw-load.exe)

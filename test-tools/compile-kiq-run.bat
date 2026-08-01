@echo off
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "EXTRA_INC=F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared;F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um"
set "EXTRA_LIB=F:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;F:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
set "INCLUDE=%EXTRA_INC%;%INCLUDE%"
set "LIB=%EXTRA_LIB%;%LIB%"
cl /nologo /W3 /O2 test-tools\run-gpu-kiq-test.c /Fe:output\run-gpu-kiq-test.exe /link /SUBSYSTEM:CONSOLE
if %errorlevel% neq 0 (echo BUILD FAILED & exit /b %errorlevel%)
echo BUILD OK

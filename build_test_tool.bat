@echo off
setlocal
call "E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %errorlevel% neq 0 exit /b %errorlevel%
set INCLUDE=%INCLUDE%;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared
set LIB=%LIB%;E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64
cl /nologo /W4 /MT /O2 /Fe:C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-gpu-hw-init.exe C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-gpu-hw-init.c /link /subsystem:console /defaultlib:advapi32.lib /defaultlib:ws2_32.lib

@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "INCLUDE=D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\km\crt;D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\km;D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared;D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um"
cl.exe /nologo /W3 /O2 /D_AMD64_ /DWIN64 "%~dp0reg-dump-and-nop.c" /Fe"%~dp0..\output\reg-dump-and-nop.exe" /link /LIBPATH:"D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.44.35207\lib\onecore\x64" /LIBPATH:"D:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" /LIBPATH:"D:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" kernel32.lib advapi32.lib
if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED
    exit /b 1
)
echo BUILD OK: output\reg-dump-and-nop.exe

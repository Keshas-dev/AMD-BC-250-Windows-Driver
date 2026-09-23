@echo off
setlocal
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not defined INCLUDE (
  call "F:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
)
set "WDK=F:\Program Files (x86)\Windows Kits\10"
set "WINVER=10.0.26100.0"
set "INCLUDE=%WDK%\Include\%WINVER%\ucrt;%WDK%\Include\%WINVER%\shared;%WDK%\Include\%WINVER%\um;%WDK%\Include\%WINVER%\winrt;%INCLUDE%"
set "LIB=%WDK%\Lib\%WINVER%\ucrt\x64;%WDK%\Lib\%WINVER%\um\x64;%LIB%"
cd /d "%~dp0"
cl /nologo /O2 /utf-8 /W3 /Fe..\output\safe-probe.exe safe-probe.c /link /subsystem:console user32.lib
if %errorlevel% equ 0 (echo BUILD OK: output\safe-probe.exe) else (echo BUILD FAILED)
endlocal

@echo off
setlocal enabledelayedexpansion

call "E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set "WINVER=10.0.22621.0"
set "WDK=E:\Program Files (x86)\Windows Kits\10"
set "MSVC=E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
set "VCTools=E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"

set "INCLUDE=%VCTools%\include;%WDK%\Include\%WINVER%\ucrt;%WDK%\Include\%WINVER%\shared;%WDK%\Include\%WINVER%\um;%WDK%\Include\%WINVER%\winrt"
set "LIB=%VCTools%\lib\x64;%WDK%\Lib\%WINVER%\ucrt\x64;%WDK%\Lib\%WINVER%\um\x64"

cl /nologo /O2 /W3 bar2-kiq-test.c /Fe..\output\bar2-kiq-test.exe /link /subsystem:console
if %ERRORLEVEL% equ 0 ( echo bar2-kiq-test.exe compiled ) else ( echo COMPILE FAILED )

endlocal

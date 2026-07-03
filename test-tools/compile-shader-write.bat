@echo off
setlocal
call "E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 call "E:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set WINVER=10.0.22621.0
set WDK=E:\Program Files (x86)\Windows Kits\10
set VCTools=E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set INCLUDE=%VCTools%\include;%WDK%\Include\%WINVER%\ucrt;%WDK%\Include\%WINVER%\shared;%WDK%\Include\%WINVER%\um;%WDK%\Include\%WINVER%\winrt
set LIB=%VCTools%\lib\x64;%WDK%\Lib\%WINVER%\ucrt\x64;%WDK%\Lib\%WINVER%\um\x64
cl /nologo /O2 /utf-8 /W3 /Fe"shader-write-test" shader-write-test.c /link /subsystem:console
cl /nologo /O2 /utf-8 /W3 /Fe"dc60-probe-test" dc60-probe-test.c /link /subsystem:console
cl /nologo /O2 /utf-8 /W3 /Fe"gcvm-mqd-test" gcvm-mqd-test.c /link /subsystem:console
cl /nologo /O2 /utf-8 /W3 /Fe"patched-mec-ring-test" patched-mec-ring-test.c /link /subsystem:console
cl /nologo /O2 /utf-8 /W3 /Fe"hqd-probe-test" hqd-probe-test.c /link /subsystem:console
cl /nologo /O2 /utf-8 /W3 /Fe"sdma-probe-test" sdma-probe-test.c /link /subsystem:console
cl /nologo /O2 /utf-8 /W3 /Fe"clean-state-test" clean-state-test.c /link /subsystem:console
cl /nologo /O2 /utf-8 /W3 /Fe"rlc-mec-ring-test" rlc-mec-ring-test.c /link /subsystem:console
cl /nologo /O2 /utf-8 /W3 /Fe"mqd-dim-test" mqd-dim-test.c /link /subsystem:console
cl /nologo /O2 /utf-8 /W3 /Fe"mqd-wptr-test" mqd-wptr-test.c /link /subsystem:console
if %errorlevel% equ 0 (echo Build OK) else (echo Build FAILED)
endlocal

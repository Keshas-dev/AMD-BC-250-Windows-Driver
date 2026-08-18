@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set INCLUDE=MSVC_INC;WDK_ucrt;WDK_shared;WDK_um;WDK_km;%INCLUDE%
set LIB=MSVC_lib;WDK_ucrt\x64;WDK_um\x64;%LIB%
cl /nologo /O2 /W3 /Feoutput\test-gpu-ioctls.exe test-tools\test-gpu-ioctls.c /link /subsystem:console
cl /nologo /O2 /W3 /Feoutput\sdma-selftest.exe test-tools\sdma-selftest.c /link /subsystem:console
cl /nologo /O2 /W3 /Feoutput\bar5-smn-test.exe test-tools\bar5-smn-test.c /link /subsystem:console
cl /nologo /O2 /W3 /Feoutput\psp-fw-load.exe test-tools\psp-fw-load.c /link /subsystem:console
pause

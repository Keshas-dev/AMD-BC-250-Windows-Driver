@echo off
REM Compile patched-mec-test.exe
REM Requires VS2022 x64 Native Tools prompt

set SRC=%~dp0patched-mec-test.c
set OUT=%~dp0patched-mec-test.exe

cl /nologo /O2 /W3 /utf-8 /Fe%OUT% %SRC% /link /subsystem:console
if %ERRORLEVEL% equ 0 (
    echo Compiled: %OUT%
) else (
    echo COMPILE FAILED
)

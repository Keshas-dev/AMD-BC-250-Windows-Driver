@echo off
setlocal enabledelayedexpansion
if not defined VSCMD_VER (
    for /f "usebackq" %%a in (`dir /b /od "%ProgramFiles%\Microsoft Visual Studio\2022" 2^>nul ^| findstr /i "professional enterprise community"`) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%a\VC\Auxiliary\Build\vcvars64.bat" (
            call "%ProgramFiles%\Microsoft Visual Studio\2022\%%a\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
        )
    )
)
cl /nologo /O2 /W3 bar2-kiq-test.c /Fe..\output\bar2-kiq-test.exe /link /subsystem:console
if %ERRORLEVEL% equ 0 ( echo bar2-kiq-test.exe compiled ) else ( echo COMPILE FAILED )

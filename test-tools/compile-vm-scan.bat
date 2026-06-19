@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: VS2022 x64 environment failed
    exit /b 1
)
echo Compiling vm-path-scan...
cl /nologo /O2 /W3 /Fe:output\vm-path-scan.exe vm-path-scan.c /link advapi32.lib
if %errorlevel% neq 0 (
    echo FAILED
    exit /b 1
)
echo OK: output\vm-path-scan.exe

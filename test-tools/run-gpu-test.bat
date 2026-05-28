@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl.exe test-gpu-simple.c /Fe:test-gpu-simple.exe /link d3d12.lib dxgi.lib dxguid.lib
if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation successful! Running test...
    echo.
    test-gpu-simple.exe
) else (
    echo.
    echo Compilation FAILED!
)
pause

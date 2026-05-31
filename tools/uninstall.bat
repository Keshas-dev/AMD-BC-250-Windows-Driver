@echo off
setlocal
echo ====================================================
echo   AMD BC-250 Dream Drivers v4.3 - Uninstaller
echo ====================================================
echo.

rem --- Check admin privileges ---
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] This uninstaller requires Administrator privileges!
    pause
    exit /b 1
)

set "SYS32=C:\Windows\System32"
set "DRIVERS=C:\Windows\System32\drivers"
set "SYSWOW64=C:\Windows\SysWOW64"

rem --- Stop driver ---
echo [1/5] Stopping driver...
sc stop AMDBC250DreamV43 >nul 2>&1
sc delete AMDBC250DreamV43 >nul 2>&1
echo   Done.

rem --- Remove kernel driver ---
echo [2/5] Removing kernel driver...
del /f /q "%DRIVERS%\atikmdag.sys" 2>nul
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43" /f >nul 2>&1
echo   Done.

rem --- Remove user-mode DLLs ---
echo [3/5] Removing user-mode DLLs...
del /f /q "%SYS32%\amdbc250umd64.dll" 2>nul
del /f /q "%SYS32%\amdbc250vulkan.dll" 2>nul
del /f /q "%SYS32%\amdbc250_icd.json" 2>nul
echo   Done.

rem --- Remove Vulkan ICD registration ---
echo [4/5] Removing Vulkan ICD registration...
reg delete "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /f >nul 2>&1
echo   Done.

rem --- Remove DXVK (restore Windows defaults) ---
echo [5/5] Removing DXVK and DXVK-NVAPI...
echo   NOTE: Windows will restore default D3D DLLs on reboot.
echo   If DirectX issues occur, run: sfc /scannow
del /f /q "%SYS32%\d3d9.dll" 2>nul
del /f /q "%SYS32%\d3d11.dll" 2>nul
del /f /q "%SYS32%\d3d10core.dll" 2>nul
del /f /q "%SYS32%\dxgi.dll" 2>nul
del /f /q "%SYSWOW64%\d3d9.dll" 2>nul
del /f /q "%SYSWOW64%\d3d11.dll" 2>nul
del /f /q "%SYSWOW64%\d3d10core.dll" 2>nul
del /f /q "%SYSWOW64%\dxgi.dll" 2>nul
del /f /q "%SYS32%\nvapi64.dll" 2>nul
del /f /q "%SYS32%\nvofapi64.dll" 2>nul
del /f /q "%SYSWOW64%\nvapi.dll" 2>nul
echo   Done.

echo.
echo ====================================================
echo   Uninstallation Complete!
echo ====================================================
echo   Reboot recommended.
echo.
pause

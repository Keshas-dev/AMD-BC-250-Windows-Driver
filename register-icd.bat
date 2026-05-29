@echo off
reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_icd.json" /t REG_DWORD /d 0 /f
echo.
reg query "HKLM\SOFTWARE\Khronos\Vulkan\Drivers"
pause

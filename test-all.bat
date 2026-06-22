@echo off
build.bat >nul 2>&1
if %errorlevel% neq 0 exit /b 1
compile-bios-test.bat
output\bios-ring-test.exe
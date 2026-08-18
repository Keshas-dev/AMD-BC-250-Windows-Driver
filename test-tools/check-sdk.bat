@echo off
echo SDK include directories:
dir /s /b "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\*.h" 2>nul | head -3
echo ---
if exist "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared\windows.h" (echo FOUND) else (echo NOT FOUND)
dir /b "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\"

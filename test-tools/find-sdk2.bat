@echo off
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v KitsRoot10 2>nul
echo ---
dir /b "C:\Program Files (x86)\Windows Kits\10\Include" 2>nul
echo ---
dir /b "F:\Program Files (x86)\Windows Kits\10\Include" 2>nul
echo ---
dir /b "D:\Program Files (x86)\Windows Kits\10\Include" 2>nul
echo ---
dir /b "E:\Program Files (x86)\Windows Kits\10\Include" 2>nul

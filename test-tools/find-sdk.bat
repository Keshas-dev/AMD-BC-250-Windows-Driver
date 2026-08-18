@echo off
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
echo Looking for Windows Kits...
dir /s /b "F:\Program Files (x86)\Windows Kits\10\Include\*.h" 2>nul | head -3
dir /s /b "C:\Program Files (x86)\Windows Kits\10\Include\*.h" 2>nul | head -3
dir /s /b "D:\Program Files (x86)\Windows Kits\10\Include\*.h" 2>nul | head -3
dir /s /b "E:\Program Files (x86)\Windows Kits\10\Include\*.h" 2>nul | head -3
reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v KitsRoot10 2>nul

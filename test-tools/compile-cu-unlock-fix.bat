@echo off
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools
cl /nologo /O2 /utf-8 /W3 /FeC:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\bar5-cu-unlock-test.exe bar5-cu-unlock-test.c /link /subsystem:console

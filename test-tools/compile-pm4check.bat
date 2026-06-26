@echo off
call "E:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl pm4check.c /nologo /Fecheck.exe
check.exe
del pm4check.c check.exe 2>nul

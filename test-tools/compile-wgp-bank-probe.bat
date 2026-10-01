@echo off
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /O2 /utf-8 /W3 /Fe..\output\wgp-bank-probe.exe wgp-bank-probe.c /link /subsystem:console

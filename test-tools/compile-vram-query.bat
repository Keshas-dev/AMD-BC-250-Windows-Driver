@echo off
set "CL=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe"
"%CL%" /nologo /W3 /O2 /D_AMD64_ /DWIN64 /I"F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" /I"F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" /I"F:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt" "%~dp0vram-query.c" /Fe"%~dp0..\output\vram-query.exe" /link /LIBPATH:"F:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" /LIBPATH:"F:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" kernel32.lib

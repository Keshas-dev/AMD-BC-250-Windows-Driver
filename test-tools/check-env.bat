@echo off
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
echo CL path: 
where cl
echo INCLUDE=%INCLUDE%
echo LIB=%LIB%
dir /b "E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" 2>&1
dir /b "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" 2>&1

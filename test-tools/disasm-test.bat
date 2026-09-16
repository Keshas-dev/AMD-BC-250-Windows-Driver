@echo off
setlocal
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
dumpbin /DISASM /OUT:"%~dp0radv-disasm.txt" "%~dp0..\output\radv-backend-test.exe"
endlocal

@echo off
setlocal
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "WINVER=10.0.26100.0"
set "WDK=F:\Program Files (x86)\Windows Kits\10"
set "VCTools=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
set "INCLUDE=%VCTools%\include;%WDK%\Include\%WINVER%\ucrt;%WDK%\Include\%WINVER%\shared;%WDK%\Include\%WINVER%\um;%WDK%\Include\%WINVER%\winrt"
set "LIB=%VCTools%\lib\x64;%WDK%\Lib\%WINVER%\ucrt\x64;%WDK%\Lib\%WINVER%\um\x64"
set "PATH=%WDK%\bin\%WINVER%\x64;C:\Users\Keshas\AppData\Local\Temp\opencode\ninja;%PATH%"
cd /d F:\mesa-build
C:\Users\Keshas\AppData\Local\Temp\opencode\ninja\ninja.exe %*
echo NINJA_EXIT=%errorlevel%
endlocal

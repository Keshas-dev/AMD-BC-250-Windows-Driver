@echo off
setlocal
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "WINVER=10.0.26100.0"
set "WDK=F:\Program Files (x86)\Windows Kits\10"
set "VCTools=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
set "INCLUDE=%VCTools%\include;%WDK%\Include\%WINVER%\ucrt;%WDK%\Include\%WINVER%\shared;%WDK%\Include\%WINVER%\um;%WDK%\Include\%WINVER%\winrt"
set "LIB=%VCTools%\lib\x64;%WDK%\Lib\%WINVER%\ucrt\x64;%WDK%\Lib\%WINVER%\um\x64"
set "PATH=%WDK%\bin\%WINVER%\x64;%PATH%"
set "MESON=C:\Users\Keshas\AppData\Roaming\Python\Python314\Scripts\meson.exe"
set "NINJA=C:\Users\Keshas\AppData\Local\Temp\opencode\ninja\ninja.exe"
set "PATH=C:\Users\Keshas\AppData\Local\Temp\opencode\ninja;%PATH%"
set "SRC=C:\Users\Keshas\AppData\Local\Temp\opencode\mesa-wdm"
set "BLD=F:\mesa-build"
if "%1"=="wipe" (
  echo Wiping build dir...
  rmdir /s /q "%BLD%"
)
if not exist "%BLD%" mkdir "%BLD%"
cd /d "%BLD%"
"%MESON%" setup "%SRC%" ^
  --backend=ninja ^
  --buildtype=release ^
  -Dvulkan-drivers=amd ^
  -Dgallium-drivers= ^
  -Dopengl=false ^
  -Degl=disabled ^
  -Dgbm=disabled ^
  -Dllvm=disabled ^
  -Dshared-llvm=disabled ^
  -Damd-use-llvm=false ^
  -Dtools= ^
  -Dbuild-tests=false
echo MESON_EXIT=%errorlevel%
endlocal

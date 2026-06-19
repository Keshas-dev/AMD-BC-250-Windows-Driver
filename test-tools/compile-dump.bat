@echo off
setlocal
call "D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "SDK=D:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
set "SDKLIB=D:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"
set "VS=D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.44.35207"
cl.exe /nologo /W3 /O2 /D_AMD64_ /DWIN64 ^
  /I"%SDK%\um" /I"%SDK%\shared" /I"%SDK%\ucrt" /I"%SDK%\winrt" ^
  /I"%SDK%\km\crt" /I"%SDK%\km" ^
  "%~dp0dump-fresh-boot.c" /Fe"%~dp0..\output\dump-fresh-boot.exe" ^
  /link ^
  /LIBPATH:"%VS%\lib\onecore\x64" ^
  /LIBPATH:"%SDKLIB%\ucrt\x64" ^
  /LIBPATH:"%SDKLIB%\um\x64" ^
  kernel32.lib user32.lib advapi32.lib libucrt.lib
if %errorlevel% neq 0 (echo BUILD FAILED & exit /b %errorlevel%)
echo BUILD OK: dump-fresh-boot.exe

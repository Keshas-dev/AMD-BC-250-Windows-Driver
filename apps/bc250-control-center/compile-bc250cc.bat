@echo off
setlocal
:: compile-bc250cc.bat — build BC-250 Control Center (Win32 GUI)
set SRCDIR=%~dp0
set ODIR=%~dp0..\..\output
if not exist "%ODIR%" mkdir "%ODIR%"

:: Locate VS2022 (BuildTools or Community, any drive)
set VCVARS=
for %%D in (C D E F G) do (
  if not defined VCVARS (
    if exist "%%D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if exist "%%D:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if exist "%%D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%%D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "%%D:\VS2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%%D:\VS2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
  )
)
if not defined VCVARS (
  echo ERROR: VS2022 BuildTools/Community vcvars64.bat not found.
  exit /b 1
)

:: Determine WDK/SDK root
set SDK=
for %%D in (C D E F G) do (
  if not defined SDK (
    if exist "%%D:\Program Files (x86)\Windows Kits\10" set "SDK=%%D:\Program Files (x86)\Windows Kits\10"
  )
)
if not defined SDK (
  echo ERROR: Windows Kits\10 not found.
  exit /b 1
)
set SDKVER=10.0.26100.0
if not exist "%SDK%\Lib\%SDKVER%\um\x64" set SDKVER=10.0.22621.0
if not exist "%SDK%\Lib\%SDKVER%\um\x64" set SDKVER=10.0.19041.0

call "%VCVARS%" x64 >nul
cl /nologo /O2 /utf-8 /MD /W3 /wd4100 /wd4101 /wd4189 /wd4996 ^
  /I"%SRCDIR%..\..\inc" ^
  /I"%SDK%\Include\%SDKVER%\um" ^
  /I"%SDK%\Include\%SDKVER%\shared" ^
  /I"%SDK%\Include\%SDKVER%\ucrt" ^
  /Fe"%ODIR%\BC250CC.exe" ^
  "%SRCDIR%bc250cc.c" ^
  /link /subsystem:windows ^
  /libpath:"%SDK%\Lib\%SDKVER%\um\x64" /libpath:"%SDK%\Lib\%SDKVER%\ucrt\x64" ^
  kernel32.lib user32.lib gdi32.lib comctl32.lib comdlg32.lib advapi32.lib
if errorlevel 1 (echo BUILD FAILED & exit /b 1) else (echo OK: output\BC250CC.exe)

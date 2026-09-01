@echo off
setlocal
set "PROJECT_DIR=%~dp0"
set "SRC=%PROJECT_DIR%src\early_wgp.c"
set "INF=%PROJECT_DIR%inf\earlywgp.inf"
set "OUT=%PROJECT_DIR%..\output\earlywgp"
if not exist "%OUT%" mkdir "%OUT%"
REM detect VS
set "VSWHERE="
if exist "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VSWHERE=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VSWHERE=D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if "%VSWHERE%"=="" (echo VS not found & exit /b 1)
call "%VSWHERE%" >nul 2>&1
REM detect WDK
set "WDK_ROOT="
if exist "F:\Program Files (x86)\Windows Kits\10\Include" set "WDK_ROOT=F:\Program Files (x86)\Windows Kits\10"
if exist "D:\Program Files (x86)\Windows Kits\10\Include" set "WDK_ROOT=D:\Program Files (x86)\Windows Kits\10"
for /f "delims=" %%V in ('dir /b /ad "%WDK_ROOT%\Include" ^| sort /r') do (
  if exist "%WDK_ROOT%\Include\%%V\km\ntddk.h" (set "WDK_VERSION=%%V" & goto :found)
)
:found
echo WDK %WDK_VERSION% at %WDK_ROOT%
set "CERT_SHA1=34AFF96C57E9ADE68B23B4828859CF9B7F4EF442"
cl /c /kernel /W3 /Zi /Od /DAMD64 /D_AMD64_ /GS- /I"%WDK_ROOT%\Include\%WDK_VERSION%\km" /I"%WDK_ROOT%\Include\%WDK_VERSION%\km\crt" /I"%WDK_ROOT%\Include\%WDK_VERSION%\shared" "%SRC%" /Fo"%OUT%\early_wgp.obj"
if errorlevel 1 exit /b 1
link /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /OUT:"%OUT%\earlywgp.sys" "%OUT%\early_wgp.obj" ntoskrnl.lib hal.lib wdm.lib /LIBPATH:"%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64"
if errorlevel 1 exit /b 1
copy "%INF%" "%OUT%\" >nul
REM sign
set "SIGNTOOLS=%WDK_ROOT%\bin\%WDK_VERSION%\x64"
if not exist "%SIGNTOOLS%\signtool.exe" set "SIGNTOOLS=%WDK_ROOT%\bin\%WDK_VERSION%\x86"
"%SIGNTOOLS%\signtool.exe" sign /fd SHA256 /sha1 %CERT_SHA1% "%OUT%\earlywgp.sys"
"%SIGNTOOLS%\signtool.exe" verify /pa /v "%OUT%\earlywgp.sys"
if exist "%SIGNTOOLS%\Inf2Cat.exe" (
  del /q "%OUT%\earlywgp.cat" 2>nul
  "%WDK_ROOT%\bin\%WDK_VERSION%\x86\Inf2Cat.exe" /driver:"%OUT%" /os:10_x64 /verbose
  "%SIGNTOOLS%\signtool.exe" sign /fd SHA256 /sha1 %CERT_SHA1% "%OUT%\earlywgp.cat"
)
echo BUILD EARLYWGP DONE at %OUT%

@echo off
rem Build the BC-250 UEFI WGP probe as PE32+ EFI applications using MSVC.
rem
rem Three binaries, so a SMU wedge in one test cannot destroy the results of
rem another, and each writes its own log:
rem
rem   bc250-wgp-chain.efi  TEST_MODE=1  chain + baseline only, WGP0.LOG
rem   bc250-wgp-probe.efi  TEST_MODE=2  chain + wgp_probe,       WGP1.LOG
rem   bc250-wgp-full.efi   TEST_MODE=0  everything + final state, WGP.LOG
rem
rem Build one only:  build-msvc.bat chain|probe|full
setlocal
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
  echo ERROR: vcvars64 failed - is the F: drive mounted?
  exit /b 1
)
cd /d "%~dp0"
if not exist output mkdir output

set "ONLY=%~1"
if "%ONLY%"=="" set "ONLY=all"

set "CFLAGS=/nologo /c /O2 /GS- /GR- /Zl /W3 /I vendor /Fo:output\"
set "LFLAGS=/nologo /NODEFAULTLIB /ENTRY:efi_main /SUBSYSTEM:EFI_APPLICATION /MACHINE:X64 /OPT:REF /OPT:ICF"
set "OBJS=output\smu.obj output\unlock.obj output\patches.obj output\wgp.obj output\elog.obj output\main.obj"

call :build 1 chain
call :build 2 probe
call :build 0 full

echo.
dir output\*.efi
endlocal
exit /b 0

:build
if /i not "%ONLY%"=="all" if /i not "%ONLY%"=="%~2" goto :eof
echo --- TEST_MODE=%~1  ->  %~2.efi
cl %CFLAGS% /DTEST_MODE=%~1 smu.c unlock.c patches.c wgp.c elog.c main.c
if errorlevel 1 ( echo   BUILD FAILED - compile & exit /b 1 )
link %LFLAGS% /OUT:output\bc250-wgp-%~2.efi %OBJS%
if errorlevel 1 ( echo   BUILD FAILED - link & exit /b 1 )
goto :eof

@echo off
rem Build the BC-250 UEFI WGP probe as PE32+ EFI applications using MSVC.
rem
rem Two binaries come out of this:
rem   bc250-wgp-smoke.efi  TEST_MODE=1 - opens the log, prints, touches no SMU.
rem   bc250-wgp-probe.efi  TEST_MODE=2 - full unlock chain + WGP hypotheses.
rem
rem Run the smoke one first. The previous build produced no output of any kind,
rem and there is no way to tell from the screen whether that means the firmware
rem never called efi_main or that the logging path itself is broken. The smoke
rem build answers that without touching the SMU.
rem
rem Override with:  build-msvc.bat full
rem
setlocal
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
  echo ERROR: vcvars64 failed
  exit /b 1
)
cd /d "%~dp0"
if not exist output mkdir output

set "MODE=both"
if /i "%~1"=="full" set "MODE=full"
if /i "%~1"=="smoke" set "MODE=smoke"

rem /GS- /GR- : no stack cookie or RTTI in a freestanding EFI image
set "CFLAGS=/nologo /c /O2 /GS- /GR- /Zl /W3 /I vendor /Fo:output\"
cl %CFLAGS% /DTEST_MODE=1 smu.c unlock.c patches.c wgp.c elog.c early.c main.c
if errorlevel 1 ( echo BUILD FAILED - compile stage & exit /b 1 )
link /nologo /NODEFAULTLIB /ENTRY:efi_main /SUBSYSTEM:EFI_APPLICATION ^
     /MACHINE:X64 /OPT:REF /OPT:ICF ^
     /OUT:output\bc250-wgp-smoke.efi ^
     output\smu.obj output\unlock.obj output\patches.obj ^
     output\wgp.obj output\elog.obj output\early.obj output\main.obj
if errorlevel 1 ( echo BUILD FAILED - link stage (smoke) & exit /b 1 )
echo smoke  build OK

if /i "%MODE%"=="smoke" goto done

cl %CFLAGS% /DTEST_MODE=2 smu.c unlock.c patches.c wgp.c elog.c early.c main.c
if errorlevel 1 ( echo BUILD FAILED - compile stage (full) & exit /b 1 )
link /nologo /NODEFAULTLIB /ENTRY:efi_main /SUBSYSTEM:EFI_APPLICATION ^
     /MACHINE:X64 /OPT:REF /OPT:ICF ^
     /OUT:output\bc250-wgp-probe.efi ^
     output\smu.obj output\unlock.obj output\patches.obj ^
     output\wgp.obj output\elog.obj output\early.obj output\main.obj
if errorlevel 1 ( echo BUILD FAILED - link stage (full) & exit /b 1 )
echo full   build OK

:done
echo.
dir output\*.efi
endlocal

@echo off
rem Build the BC-250 UEFI WGP probe as a PE32+ EFI application using MSVC.
rem
rem Console output only. The file-logging (elog.c) and run-marker (early.c)
rem modules are not part of this binary - neither produced any output on this
rem board and neither added anything over the console.
rem
rem Output: uefi\wgp-test\output\bc250-wgp-probe.efi
setlocal
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
  echo ERROR: vcvars64 failed
  exit /b 1
)
cd /d "%~dp0"
if not exist output mkdir output

rem /GS- /GR- : no stack cookie or RTTI in a freestanding EFI image
cl /nologo /c /O2 /GS- /GR- /Zl /W3 /I vendor /Fo:output\ ^
   smu.c unlock.c patches.c wgp.c main.c
if errorlevel 1 (
  echo BUILD FAILED - compile stage
  exit /b 1
)

rem /NODEFAULTLIB : no CRT, the sources provide their own memcpy/memset.
rem /SUBSYSTEM:EFI_APPLICATION (= 10) and /ENTRY:efi_main are what makes the
rem firmware loader treat this as a bootable application.
link /nologo /NODEFAULTLIB /ENTRY:efi_main /SUBSYSTEM:EFI_APPLICATION ^
     /MACHINE:X64 /OPT:REF /OPT:ICF ^
     /OUT:output\bc250-wgp-probe.efi ^
     output\smu.obj output\unlock.obj output\patches.obj ^
     output\wgp.obj output\main.obj
if errorlevel 1 (
  echo BUILD FAILED - link stage
  exit /b 1
)

echo.
echo BUILD OK
dir output\bc250-wgp-probe.efi
endlocal

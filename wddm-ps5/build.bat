@echo off
echo AMD BC-250 WDDM Miniport (wddm-ps5) Build
echo =========================================
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0src\kmd"
if not exist "build\x64\Release" mkdir "build\x64\Release"
set KIT=F:\Program Files (x86)\Windows Kits\10
set KVER=10.0.26100.0
set MSVC=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set OUT=build\x64\Release

"%MSVC%\bin\HostX64\x64\CL.exe" /c /I"." /I"..\common" /I"%KIT%\Include\%KVER%\km" /I"%KIT%\Include\%KVER%\shared" /I"%KIT%\Include\%KVER%\ucrt" /Zi /nologo /W3 /WX- /diagnostics:column /Ox /Os /Oy- /D _AMD64_ /D _WIN64 /D AMD64 /D _WIN32_WINNT=0x0A00 /D WINVER=0x0A00 /D WINNT=1 /D NTDDI_VERSION=0xA000010 /D NDEBUG /GF /Gm- /Zp8 /GS /guard:cf /Gy /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /GR- /Fo"%OUT%\\" /Fd"%OUT%\vc143.pdb" /Gz /FC /errorReport:queue /kernel -cbstring -d2epilogunwind /d1nodatetime amdbc250_kmd.c amdbc250_hw_init.c
if errorlevel 1 goto :err
echo === LINK ===
"%MSVC%\bin\HostX64\x64\link.exe" /ERRORREPORT:QUEUE /OUT:"%OUT%\amdbc250kmd.sys" /VERSION:"1.0" /INCREMENTAL:NO /NOLOGO /WX /SECTION:"INIT,d" "%KIT%\lib\%KVER%\km\x64\BufferOverflowFastFailK.lib" "%KIT%\lib\%KVER%\km\x64\ntoskrnl.lib" "%KIT%\lib\%KVER%\km\x64\hal.lib" "%KIT%\lib\%KVER%\km\x64\wmilib.lib" "%KIT%\lib\%KVER%\km\x64\displib.lib" "%KIT%\lib\%KVER%\km\x64\ntoskrnl.lib" /NODEFAULTLIB /MANIFEST:NO /DEBUG /PDB:"%OUT%\amdbc250kmd.pdb" /SUBSYSTEM:NATIVE,"10.00" /Driver /OPT:REF /OPT:ICF /ENTRY:"DriverEntry" /RELEASE /IMPLIB:"%OUT%\amdbc250kmd.lib" /MACHINE:X64 /guard:cf /kernel /IGNORE:4198,4010,4037,4039,4065,4070,4078,4087,4089,4221,4108,4088,4218,4235 /osversion:10.0 /pdbcompress /debugtype:pdata "%OUT%\amdbc250_kmd.obj" "%OUT%\amdbc250_hw_init.obj"
if errorlevel 1 goto :err
echo === DONE ===
exit /b 0
:err
echo BUILD FAILED
exit /b 1

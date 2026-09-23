@echo off
echo AMD BC-250 WDDM Full Stack (wddm-ps5) Build
echo ============================================
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set KIT=F:\Program Files (x86)\Windows Kits\10
set KVER=10.0.26100.0
set MSVC=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207

echo.
echo === Building KMD (amdbc250kmd.sys) ===
cd /d "%~dp0src\kmd"
if not exist "build\x64\Release" mkdir "build\x64\Release"
set OUT=build\x64\Release

"%MSVC%\bin\HostX64\x64\CL.exe" /c /I"." /I"..\common" /I"%KIT%\Include\%KVER%\km" /I"%KIT%\Include\%KVER%\shared" /I"%KIT%\Include\%KVER%\ucrt" /Zi /nologo /W3 /WX- /diagnostics:column /Ox /Os /Oy- /D _AMD64_ /D _WIN64 /D AMD64 /D _WIN32_WINNT=0x0A00 /D WINVER=0x0A00 /D WINNT=1 /D NTDDI_VERSION=0xA000010 /D NDEBUG /GF /Gm- /Zp8 /GS /guard:cf /Gy /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /GR- /Fo"%OUT%\\" /Fd"%OUT%\vc143.pdb" /Gz /FC /errorReport:queue /kernel -cbstring -d2epilogunwind /d1nodatetime amdbc250_kmd.c amdbc250_hw_init.c
if errorlevel 1 goto :err

echo === LINK KMD ===
"%MSVC%\bin\HostX64\x64\link.exe" /ERRORREPORT:QUEUE /OUT:"%OUT%\amdbc250kmd.sys" /VERSION:"1.0" /INCREMENTAL:NO /NOLOGO /WX /SECTION:"INIT,d" "%KIT%\lib\%KVER%\km\x64\BufferOverflowFastFailK.lib" "%KIT%\lib\%KVER%\km\x64\ntoskrnl.lib" "%KIT%\lib\%KVER%\km\x64\hal.lib" "%KIT%\lib\%KVER%\km\x64\wmilib.lib" "%KIT%\lib\%KVER%\km\x64\displib.lib" "%KIT%\lib\%KVER%\km\x64\ntoskrnl.lib" /NODEFAULTLIB /MANIFEST:NO /DEBUG /PDB:"%OUT%\amdbc250kmd.pdb" /SUBSYSTEM:NATIVE,"10.00" /Driver /OPT:REF /OPT:ICF /ENTRY:"DriverEntry" /RELEASE /IMPLIB:"%OUT%\amdbc250kmd.lib" /MACHINE:X64 /guard:cf /kernel /IGNORE:4198,4010,4037,4039,4065,4070,4078,4087,4089,4221,4108,4088,4218,4235 /osversion:10.0 /pdbcompress /debugtype:pdata "%OUT%\amdbc250_kmd.obj" "%OUT%\amdbc250_hw_init.obj"
if errorlevel 1 goto :err
echo KMD OK: %OUT%\amdbc250kmd.sys

echo.
echo === Building UMD (amdbc250umd64.dll) ===
cd /d "%~dp0src\umd"
if not exist "build\x64\Release" mkdir "build\x64\Release"
set UMDOUT=build\x64\Release

"%MSVC%\bin\HostX64\x64\CL.exe" /c /I"%KIT%\Include\%KVER%\um" /I"%KIT%\Include\%KVER%\shared" /I"%KIT%\Include\%KVER%\ucrt" /Zi /nologo /W3 /WX- /O2 /D _AMD64_ /D _WIN64 /D AMD64 /D _WIN32_WINNT=0x0A00 /D WINVER=0x0A00 /D NDEBUG /D WIN32_LEAN_AND_MEAN /GF /Gm- /Zp8 /GS /guard:cf /Gy /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /GR- /EHsc /Fo"%UMDOUT%\\" /Fd"%UMDOUT%\vc143.pdb" /FC /errorReport:queue /DWIN32 /D_WINDOWS /D_USRDLL /DDLL /D_WINDLL amdbc250_umd.c
if errorlevel 1 goto :err

echo === LINK UMD ===
"%MSVC%\bin\HostX64\x64\link.exe" /ERRORREPORT:QUEUE /OUT:"%UMDOUT%\amdbc250umd64.dll" /INCREMENTAL:NO /NOLOGO /DEBUG /PDB:"%UMDOUT%\amdbc250umd64.pdb" /SUBSYSTEM:WINDOWS,"10.00" /OPT:REF /OPT:ICF /DLL /IMPLIB:"%UMDOUT%\amdbc250umd64.lib" /MACHINE:X64 /guard:cf /IGNORE:4198,4010,4037,4039 /osversion:10.0 /def:amdbc250umd.def /LIBPATH:"%KIT%\Lib\%KVER%\um\x64" /LIBPATH:"%KIT%\Lib\%KVER%\ucrt\x64" "%UMDOUT%\amdbc250_umd.obj" kernel32.lib user32.lib
if errorlevel 1 goto :err
echo UMD OK: %UMDOUT%\amdbc250umd64.dll

echo.
echo === Copying output ===
cd /d "%~dp0"
if not exist "output" mkdir "output"
copy "%~dp0src\kmd\build\x64\Release\amdbc250kmd.sys" "output\amdbc250kmd.sys"
copy "%~dp0src\umd\build\x64\Release\amdbc250umd64.dll" "output\amdbc250umd64.dll"
copy "inf\amdbc250.inf" "output\amdbc250.inf"
echo.
echo BUILD COMPLETE
echo === Signing and generating CAT ===
set SIGNTOOL=F:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe
set INF2CAT=F:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe
set OUT=output
"%SIGNTOOL%" sign /sha1 34AFF96C57E9ADE68B23B4828859CF9B7F4EF442 /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /d "AMD BC-250 WDDM Driver" "%OUT%\amdbc250kmd.sys"
"%SIGNTOOL%" sign /sha1 34AFF96C57E9ADE68B23B4828859CF9B7F4EF442 /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /d "AMD BC-250 WDDM Driver" "%OUT%\amdbc250umd64.dll"
"%INF2CAT%" /driver:%OUT% /os:10_0_26100 /v
echo ALL DONE
exit /b 0

:err
echo BUILD FAILED
exit /b 1

@echo off
echo ========================================
echo  BC-250 KMDOD Build with WGP Unlock
echo ========================================
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "MSVC=F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
set "KIT=F:\Program Files (x86)\Windows Kits\10"
set "KVER=10.0.26100.0"
set "SRC=%~dp0"
set "OUT=%SRC%build\x64\Release"
if not exist "%OUT%" mkdir "%OUT%"

REM Add WDK tools to PATH for signtool and Inf2Cat
set "PATH=%KIT%\bin\%KVER%\x64;%KIT%\bin\%KVER%\x86;%PATH%"

set "INC=%KIT%\Include\%KVER%\km;%KIT%\Include\%KVER%\km\crt;%KIT%\Include\%KVER%\shared"
set "LIB=%KIT%\Lib\%KVER%\km\x64"

REM Certificate and signing config
set "CERT=34AFF96C57E9ADE68B23B4828859CF9B7F4EF442"
set "PFX=C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\testcert.pfx"
set "PFXPWD=bc250sign"

echo Compiling BDD driver files...
cl /c /W3 /kernel /O2 /DBC250_UNLOCK /D_AMD64_ /DAMD64 /DWIN64 /D_WIN64 /I"%KIT%\Include\%KVER%\km" /I"%KIT%\Include\%KVER%\km\crt" /I"%KIT%\Include\%KVER%\shared" /I"%KIT%\Include\%KVER%\um" /I"%KIT%\Include\%KVER%\ucrt" /Fo"%OUT%\\" "%SRC%bdd.cxx" "%SRC%bdd_ddi.cxx" "%SRC%bdd_dmm.cxx" "%SRC%bdd_util.cxx" "%SRC%bltfuncs.cxx" "%SRC%blthw.cxx" "%SRC%memory.cxx" 2>&1

if errorlevel 1 (
    echo.
    echo BUILD FAILED!
    exit /b 1
)

echo.
echo Linking SampleDisplay.sys...
link /ERRORREPORT:QUEUE /OUT:"%OUT%\SampleDisplay.sys" /VERSION:"1.0" /INCREMENTAL:NO /NOLOGO /SECTION:"INIT,d" "%KIT%\lib\%KVER%\km\x64\BufferOverflowFastFailK.lib" "%KIT%\lib\%KVER%\km\x64\ntoskrnl.lib" "%KIT%\lib\%KVER%\km\x64\hal.lib" "%KIT%\lib\%KVER%\km\x64\wmilib.lib" "%KIT%\lib\%KVER%\km\x64\displib.lib" /NODEFAULTLIB /MANIFEST:NO /DEBUG /PDB:"%OUT%\SampleDisplay.pdb" /SUBSYSTEM:NATIVE,"10.00" /Driver /OPT:REF /OPT:ICF /ENTRY:"DriverEntry" /RELEASE /IMPLIB:"%OUT%\SampleDisplay.lib" /MACHINE:X64 /guard:cf /kernel /IGNORE:4198,4010,4037,4039,4065,4070,4078,4087,4089,4221,4108,4088,4218,4235 /osversion:10.0 /pdbcompress /debugtype:pdata "%OUT%\bdd.obj" "%OUT%\bdd_ddi.obj" "%OUT%\bdd_dmm.obj" "%OUT%\bdd_util.obj" "%OUT%\bltfuncs.obj" "%OUT%\blthw.obj" "%OUT%\memory.obj" 2>&1

if errorlevel 1 (
    echo.
    echo LINK FAILED!
    exit /b 1
)

echo.
echo === BUILD OK ===
dir "%OUT%\SampleDisplay.sys"
echo.
echo Signing with self-cert...
signtool sign /v /sha1 %CERT% /fd sha256 /tr http://timestamp.digicert.com /td sha256 /f "%PFX%" /p %PFXPWD% "%OUT%\SampleDisplay.sys" 2>&1

if errorlevel 1 (
    echo.
    echo SIGN FAILED!
    exit /b 1
)

echo.
echo === SIGN OK ===
dir "%OUT%\SampleDisplay.sys"
echo.
echo Generating CAT file...
del /Q "%SRC%sampledisplay.cat" >nul 2>&1
"%KIT%\bin\%KVER%\x86\Inf2Cat.exe" /driver:"%SRC:~0,-1%" /os:10_X64 2>&1

if errorlevel 1 (
    echo.
    echo CAT GENERATION FAILED!
    exit /b 1
)

echo.
echo Copying SYS to INF directory...
copy /Y "%OUT%\SampleDisplay.sys" "%SRC%SampleDisplay.sys" >nul 2>&1
echo.
echo === ALL DONE ===
echo.
echo Files:
dir "%SRC%SampleDisplay.sys"
dir "%SRC%sampledisplay.cat"
echo.

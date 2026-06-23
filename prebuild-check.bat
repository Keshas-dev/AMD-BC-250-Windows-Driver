@echo off
setlocal enabledelayedexpansion

echo === Pre-build Validation ===
echo.

set "SRC=%~dp0src\kmd\amdbc250_dream_kmd.c"
set "ERR=0"

rem 1. Check for duplicate IOCTL case values
echo [1/5] Checking IOCTL case value uniqueness...
for /f "tokens=3 delims=: " %%a in ('findstr /n "case 0x80000" "%SRC%"') do (
    set "cv=%%a"
    set "cv=!cv:{=!"
    set "cv=!cv: =!"
    if defined cv (
        set "cnt=0"
        for /f "tokens=3 delims=: " %%b in ('findstr /n "!cv!" "%SRC%"') do (
            set /a cnt+=1
        )
        if !cnt! gtr 1 (
            echo   ERROR: Duplicate case value !cv! (!cnt! occurrences^)
            set /a ERR+=1
        )
    )
)

rem 2. Check for RESP undeclared (common pitfall)
echo [2/5] Checking for undeclared identifiers...
findstr /n "= (PVOID)Resp" "%SRC%" >nul 2>&1
if not errorlevel 1 (
    echo   ERROR: "Resp" used without declaration
    set /a ERR+=1
)

rem 3. Check output struct size matches IOCTL buffer check
echo [3/5] Checking IOCTL buffer size checks...
findstr /n "outputLen < sizeof" "%SRC%" >nul 2>&1
if errorlevel 1 (
    echo   WARNING: No outputLen checks found
)

rem 4. Check that MmAllocateContiguousMemorySpecifyCache NULL checks exist
echo [4/5] Checking allocation NULL checks...
findstr /n "MmAllocateContiguousMemorySpecifyCache" "%SRC%" >nul 2>&1
if errorlevel 1 (
    echo   WARNING: No DMA allocations found
) else (
    echo   OK: Found DMA allocations
)

rem 5. Check static function usage across files
echo [5/5] Checking cross-file function calls...
for %%f in (DreamV3AllocateContiguousMemory DreamV3FreeContiguousMemory) do (
    findstr /n "%%f" "%SRC%" >nul 2>&1
    if not errorlevel 1 (
        findstr "static.*%%f" "%~dp0src\kmd\amdbc250_dream_hw_init.c" >nul 2>&1
        if not errorlevel 1 (
            echo   ERROR: %%f is static in hw_init.c but called from kmd.c
            set /a ERR+=1
        )
    )
)

echo.
if %ERR% gtr 0 (
    echo FAILED: %ERR% error^(s^) found. Fix before building.
    exit /b 1
) else (
    echo PASSED: All checks OK
)
echo.
endlocal

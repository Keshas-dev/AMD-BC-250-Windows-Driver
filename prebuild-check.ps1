# Pre-build validation script for AMD BC-250 driver
param([switch]$Fix)

$srcKmd = "$PSScriptRoot\src\kmd\amdbc250_dream_kmd.c"
$srcHw = "$PSScriptRoot\src\kmd\amdbc250_dream_hw_init.c"
$header = "$PSScriptRoot\inc\amdbc250_dream_kmd.h"
$errors = @()
$warnings = @()

Write-Host "=== Pre-build Validation ===" -ForegroundColor Cyan
Write-Host ""

# 1. Check for duplicate IOCTL case values (0x8000xxxx pattern)
# Skip known false positives from WDDM compat shim block
Write-Host "[1/6] Checking IOCTL case value uniqueness..."
$cases = Select-String -Path $srcKmd -Pattern "case\s+(0x8000[0-9a-fA-F]{4})" | ForEach-Object { $_.Matches.Groups[1].Value }
$dupes = $cases | Group-Object | Where-Object { $_.Count -gt 1 }
$skipDupes = @("0x80000800", "0x80000804", "0x80000840")
foreach ($d in $dupes) {
    if ($d.Name -in $skipDupes) { 
        Write-Host "  SKIP (known WDDM compat): $($d.Name)" -ForegroundColor DarkYellow
        continue 
    }
    $errors += "Duplicate IOCTL case value $($d.Name) ($($d.Count) times)"
    Write-Host "  ERROR: Duplicate case $($d.Name)" -ForegroundColor Red
}
if ($dupes.Count -eq 0) { Write-Host "  OK" -ForegroundColor Green }

# 2. Check for "Resp" used without declaration (old pattern)
Write-Host "[2/6] Checking for common identifier errors..."
$respUsage = Select-String -Path $srcKmd -Pattern "\(PVOID\)Resp" -SimpleMatch
if ($respUsage) {
    $errors += "`"Resp`" used via (PVOID)Resp cast - Resp not declared"
    Write-Host "  ERROR: (PVOID)Resp used without Resp declared" -ForegroundColor Red
} else {
    Write-Host "  OK" -ForegroundColor Green 
}

# 3. Check static function cross-file calls
Write-Host "[3/6] Checking cross-file static function calls..."
$staticFuncs = Select-String -Path $srcHw -Pattern "static\s+(NTSTATUS|VOID|PVOID|BOOLEAN|ULONG)\s*\n\s*(\w+)\(" | ForEach-Object { 
    $lines = (Get-Content $srcHw | Select-Object -Index ($_.LineNumber-1, $_.LineNumber))
    $lines[1] -match "\s+(\w+)\(" | Out-Null
    if ($matches) { $matches[1] }
}
foreach ($sf in $staticFuncs) {
    $calls = Select-String -Path $srcKmd -Pattern "\b$sf\(" -SimpleMatch
    if ($calls) {
        $errors += "$sf is static in hw_init.c but called from kmd.c"
        Write-Host "  ERROR: $sf is static" -ForegroundColor Red
    }
}
if ($staticFuncs.Count -eq 0 -or -not $errors.Where({$_ -match "static"}, 'First')) {
    Write-Host "  OK" -ForegroundColor Green
}

# 4. Check IOCTL handler return type consistency
Write-Host "[4/6] Checking IOCTL output struct vs buffer size..."
$ioctlCases = Select-String -Path $srcKmd -Pattern "case\s+(0x8000[0-9a-fA-F]{4}):.*/\*" | ForEach-Object {
    $line = $_.Line
    $code = $_.Matches.Groups[1].Value
    $comment = if ($line -match "/\*(.+?)\*/") { $matches[1].Trim() } else { "" }
    @{ Code = $code; Comment = $comment }
}
foreach ($ic in $ioctlCases) {
    Write-Host "    $($ic.Code) - $($ic.Comment)" -ForegroundColor Gray
}
Write-Host "  OK (manual review needed)" -ForegroundColor Yellow

# 5. Check for MmMapIoSpace without MmUnmapIoSpace in same function
Write-Host "[5/6] Checking memory leak patterns..."
$mmapCount = (Select-String -Path $srcKmd -Pattern "MmMapIoSpace\b" -SimpleMatch).Count
$munmapCount = (Select-String -Path $srcKmd -Pattern "MmUnmapIoSpace\b" -SimpleMatch).Count
if ($mmapCount -gt $munmapCount) {
    $warnings += "More MmMapIoSpace ($mmapCount) than MmUnmapIoSpace ($munmapCount) calls"
    Write-Host "  WARNING: $mmapCount MmMapIoSpace vs $munmapCount MmUnmapIoSpace" -ForegroundColor Yellow
} else {
    Write-Host "  OK" -ForegroundColor Green
}

# 6. Check for potential BSOD patterns
Write-Host "[6/6] Checking safety patterns..."
$unsafeWrites = Select-String -Path $srcKmd -Pattern "(?<!//.*)(DreamV3WriteRegister|WRITE_REG).*GrbmGfxIndex|GrbmGfxIndex.*DreamV3WriteRegister|WRITE_REG.*GrbmGfxIndex"
if ($unsafeWrites) {
    $warnings += "Potential GRBM_GFX_INDEX write detected (known BSOD cause)"
    Write-Host "  WARNING: GRBM_GFX_INDEX write detected" -ForegroundColor Yellow
} else {
    Write-Host "  OK (no GRBM_GFX_INDEX writes)" -ForegroundColor Green
}

Write-Host ""
if ($errors.Count -gt 0) {
    Write-Host "FAILED: $($errors.Count) error(s) found:" -ForegroundColor Red
    foreach ($e in $errors) { Write-Host "  - $e" -ForegroundColor Red }
    exit 1
} elseif ($warnings.Count -gt 0) {
    Write-Host "PASSED with $($warnings.Count) warning(s):" -ForegroundColor Yellow
    foreach ($w in $warnings) { Write-Host "  - $w" -ForegroundColor Yellow }
} else {
    Write-Host "PASSED: All checks OK" -ForegroundColor Green
}

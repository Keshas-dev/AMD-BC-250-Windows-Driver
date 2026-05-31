$pnputil = "C:\Windows\System32\pnputil.exe"
$sys32 = "C:\Windows\System32"
$drivers = "C:\Windows\System32\drivers"

Write-Host "=== Step 1: Remove old files ==="
Remove-Item -Path "$drivers\atikmdag.sys" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "$sys32\amdbc250umd64.dll" -Force -ErrorAction SilentlyContinue
Write-Host "Old files removed"

Write-Host ""
Write-Host "=== Step 2: Uninstall from device manager ==="
& $pnputil /enum-devices /class "Display" 2>$null | Select-String -Pattern "Instance ID" | ForEach-Object {
    $id = ($_ -split ":")[-1].Trim()
    if ($id -like "*VEN_1002*") {
        Write-Host "Removing: $id"
        & $pnputil /remove-device "$id" /subtree 2>$null
    }
}

Write-Host ""
Write-Host "=== Step 3: Install fresh driver ==="
& $pnputil /add-driver "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_dream_v3.inf" /install 2>&1

Write-Host ""
Write-Host "=== DONE ==="
Write-Host "Reboot required!"

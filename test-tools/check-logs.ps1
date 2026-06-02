Write-Host "=== SYSTEM ERRORS (last 100) ==="
Get-WinEvent -LogName System -MaxEvents 100 -ErrorAction SilentlyContinue | Where-Object { $_.LevelDisplayName -eq 'Critical' -or $_.LevelDisplayName -eq 'Error' } | ForEach-Object { Write-Host "$($_.TimeCreated) ID=$($_.Id) $($_.LevelDisplayName): $($_.Message.Substring(0, [Math]::Min(150, $_.Message.Length)))" }

Write-Host ""
Write-Host "=== CRITICAL EVENTS (Event 41 = kernel power) ==="
Get-WinEvent -LogName System -MaxEvents 200 -ErrorAction SilentlyContinue | Where-Object { $_.Id -eq 41 } | ForEach-Object { Write-Host "$($_.TimeCreated) $($_.Message.Substring(0, [Math]::Min(200, $_.Message.Length)))" }

Write-Host ""
Write-Host "=== DISPLAY DRIVER ERRORS ==="
Get-WinEvent -LogName System -MaxEvents 200 -ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'display|video|dxg|gpu|atikmdag' } | ForEach-Object { Write-Host "$($_.TimeCreated) ID=$($_.Id): $($_.Message.Substring(0, [Math]::Min(200, $_.Message.Length)))" }

Write-Host ""
Write-Host "=== VIDEO CONTROLLERS ==="
Get-WmiObject Win32_VideoController | ForEach-Object { Write-Host "  Name: $($_.Name)  Status: $($_.Status)  Driver: $($_.DriverVersion)" }

Write-Host ""
Write-Host "=== BASIC DISPLAY DRIVER ==="
Get-WindowsDriver -Online -ErrorAction SilentlyContinue | Where-Object { $_.OriginalFileName -match 'basicdisplay|BasicRender' } | ForEach-Object { Write-Host "  $($_.OriginalFileName) $($_.Version)" }
driverquery /v 2>$null | Select-String -Pattern 'Basic|Display' | ForEach-Object { Write-Host "  $_" }

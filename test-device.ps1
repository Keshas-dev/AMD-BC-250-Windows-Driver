Write-Host "Testing device access..."
try {
    $h = New-Object System.IO.FileStream('\\.\AMDBC250DreamV43', [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::None)
    Write-Host "DEVICE OPENED OK!"
    $h.Close()
} catch {
    Write-Host "Failed: $($_.Exception.Message)"
}

Write-Host ""
Write-Host "Testing IOCTL..."
try {
    $device = New-Object System.IO.FileStream('\\.\AMDBC250DreamV43', [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
    Write-Host "DEVICE OPENED FOR IOCTL!"
    $device.Close()
} catch {
    Write-Host "IOCTL Failed: $($_.Exception.Message)"
}

# Uninstall BC-250 device from Device Manager
$deviceId = "PCI\VEN_1002&DEV_13FE*"

Write-Host "Searching for BC-250 device: $deviceId"
$device = Get-PnpDevice -InstanceId $deviceId

if ($device) {
    Write-Host "Found: $($device.FriendlyName)"
    Write-Host "Status: $($device.Status)"
    Write-Host "ConfigManagerErrorCode: $($device.ConfigManagerErrorCode)"
    Write-Host ""
    Write-Host "Uninstalling device..."
    
    # Use CIM method to uninstall
    $pnpEntity = Get-CimInstance -ClassName Win32_PnPEntity | Where-Object { $_.DeviceID -like $deviceId }
    
    if ($pnpEntity) {
        Write-Host "Uninstalling via CIM..."
        Invoke-CimMethod -InputObject $pnpEntity -MethodName Uninstall
        Write-Host "Device uninstalled!"
    } else {
        Write-Host "Device not found in CIM."
    }
} else {
    Write-Host "BC-250 device not found!"
}

Write-Host ""
Write-Host "Current Display Devices:"
Get-PnpDevice -Class Display | Select-Object Status, FriendlyName, InstanceId | Format-Table -AutoSize

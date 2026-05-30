# Check all display adapters
Write-Host "=== DISPLAY ADAPTERS ==="
Get-CimInstance Win32_VideoController | Select-Object Name, AdapterRAM, Status, PNPDeviceID, VideoProcessor | Format-List

# Check our device specifically  
Write-Host "=== BC-250 DEVICE STATUS ==="
Get-CimInstance Win32_PnPEntity | Where-Object { $_.DeviceID -like '*VEN_1002*DEV_13FE*' } | Select-Object Name, Status, ConfigManagerErrorCode, ProblemCode | Format-List

# Check driver loaded
Write-Host "=== LOADED DISPLAY DRIVERS ==="
Get-CimInstance Win32_SystemDriver | Where-Object { $_.DisplayName -like '*BC-250*' -or $_.DisplayName -like '*dream*' -or $_.Name -like '*atikmdag*' } | Select-Object Name, DisplayName, State, StartMode | Format-List

# Check D3D9 registry entries
Write-Host "=== DISPLAY DRIVER REGISTRY ==="
Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000" -Name "DriverDesc","InstalledDisplayDrivers","D3D12UMD","D3D11UMD","D3D10UMD" -ErrorAction SilentlyContinue | Format-List

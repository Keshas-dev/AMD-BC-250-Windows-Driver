Get-WmiObject Win32_PnPEntity | Where-Object { $_.DeviceID -like '*VEN_1002*' } | Select-Object Name, DeviceID, Status, ErrorDescription, ConfigManagerErrorCode | Format-List

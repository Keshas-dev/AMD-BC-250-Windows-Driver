# Check system events for GPU crash/BSOD
Get-WinEvent -LogName System -MaxEvents 50 | Where-Object {
    $_.TimeCreated -gt (Get-Date).AddMinutes(-10) -and 
    ($_.ProviderName -like '*atikmdag*' -or $_.ProviderName -like '*amdkmdag*' -or 
     $_.Message -like '*BC-250*' -or $_.Message -like '*dream*' -or
     $_.Id -eq 41 -or $_.Id -eq 116 -or $_.Id -eq 141 -or $_.Id -eq 6008 -or 
     $_.Id -eq 1001 -or $_.Level -le 2)
} | Select-Object TimeCreated, Id, LevelDisplayName, ProviderName, 
    @{N='Message';E={$_.Message.Substring(0, [Math]::Min(200, $_.Message.Length))}} | Format-List

# Get crash info from last 10 minutes
Get-WinEvent -LogName System -MaxEvents 30 | Where-Object {
    $_.TimeCreated -gt (Get-Date).AddMinutes(-10) -and (
        $_.ProviderName -like '*atikmdag*' -or 
        $_.Id -eq 41 -or $_.Id -eq 116 -or $_.Id -eq 6008 -or 
        $_.Id -eq 1001 -or $_.Level -le 2
    )
} | Select-Object TimeCreated, Id, LevelDisplayName, ProviderName, 
    @{N='Msg';E={$_.Message.Substring(0, [Math]::Min(300, $_.Message.Length))}} | Format-List

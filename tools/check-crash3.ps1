Get-WinEvent -LogName System -MaxEvents 10 | Where-Object {
    $_.TimeCreated -gt (Get-Date).AddMinutes(-5) -and (
        $_.Id -eq 1001 -or $_.Level -le 2
    )
} | Select-Object TimeCreated, Id, LevelDisplayName, ProviderName,
    @{N='Msg';E={$_.Message.Substring(0, [Math]::Min(300, $_.Message.Length))}} | Format-List

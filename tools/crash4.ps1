Get-WinEvent -LogName System -MaxEvents 10 | Where-Object {
    $_.TimeCreated -gt (Get-Date).AddMinutes(-5) -and ($_.Id -eq 1001 -or $_.Level -le 2)
} | Format-List TimeCreated, Id, LevelDisplayName, Message

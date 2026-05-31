Get-WinEvent -LogName System -MaxEvents 20 | Where-Object { $_.Id -eq 1001 -or $_.Id -eq 41 } | Format-List TimeCreated, Id, LevelDisplayName, Message

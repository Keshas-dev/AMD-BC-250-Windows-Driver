Copy-Item -LiteralPath "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\atikmdag.sys" -Destination "C:\Windows\System32\drivers\atikmdag.sys" -Force
Copy-Item -LiteralPath "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250umd64.dll" -Destination "C:\Windows\System32\amdbc250umd64.dll" -Force
Write-Host "Drivers installed. Reboot required."
Write-Host "KMD: C:\Windows\System32\drivers\atikmdag.sys"
Write-Host "UMD: C:\Windows\System32\amdbc250umd64.dll"

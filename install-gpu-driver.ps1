Stop-Service amdbc250kmd -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Copy-Item "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\atikmdag.sys" "$env:windir\System32\drivers\atikmdag.sys" -Force
Start-Sleep -Seconds 1
Start-Service amdbc250kmd
Write-Host "GPU driver updated and restarted."

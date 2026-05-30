New-Item -Path "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers" -Force | Out-Null
$jsonPath = "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_icd.json"
New-ItemProperty -Path "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers" -Name $jsonPath -Value 0 -PropertyType DWord -Force | Out-Null
Write-Host "Registered: $jsonPath"
Get-ChildItem "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers" | Select-Object PSChildName

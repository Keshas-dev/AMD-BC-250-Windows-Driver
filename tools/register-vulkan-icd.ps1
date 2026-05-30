# Register BC-250 Vulkan ICD
$jsonPath = "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_icd.json"

# Create registry paths
New-Item -Path "HKLM:\SOFTWARE\Khronos\Vulkan\ExplicitLayers" -Force | Out-Null

# Register ICD manifest
New-ItemProperty -Path "HKLM:\SOFTWARE\Khronos\Vulkan\ExplicitLayers" `
    -Name $jsonPath -Value 0 -PropertyType DWord -Force | Out-Null

# Also check user-level
New-Item -Path "HKCU:\SOFTWARE\Khronos\Vulkan\ExplicitLayers" -Force | Out-Null
New-ItemProperty -Path "HKCU:\SOFTWARE\Khronos\Vulkan\ExplicitLayers" `
    -Name $jsonPath -Value 0 -PropertyType DWord -Force | Out-Null

Write-Host "Vulkan ICD registered: $jsonPath"
Get-ItemProperty -Path "HKLM:\SOFTWARE\Khronos\Vulkan\ExplicitLayers" | Select-Object PSChildName, PSPath

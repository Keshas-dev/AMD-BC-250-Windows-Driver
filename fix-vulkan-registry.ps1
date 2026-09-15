# Fix Vulkan registry paths: C:\VulkanSDK → F:\VulkanSDK
# Run as Administrator
$ErrorActionPreference = "Stop"
$VULKAN_SDK = "F:\VulkanSDK\1.4.341.1"

$keys = @(
    "HKLM:\SOFTWARE\Khronos\Vulkan\ExplicitLayers",
    "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers"
)

foreach ($key in $keys) {
    if (Test-Path $key) {
        Write-Output "Fixing: $key"
        $vals = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if ($vals) {
            foreach ($prop in $vals.PSObject.Properties) {
                if ($prop.Name -like "*.json") {
                    $oldPath = $prop.Value
                    $newPath = $oldPath -replace "C:\\VulkanSDK", "F:\VulkanSDK"
                    if ($oldPath -ne $newPath) {
                        Write-Output "  $($prop.Name): $oldPath → $newPath"
                        Remove-ItemProperty -Path $key -Name $prop.Name -Force
                        New-ItemProperty -Path $key -Name $prop.Name -Value $newPath -PropertyType DWORD -Force | Out-Null
                    }
                }
            }
        }
    } else {
        Write-Output "SKIP (not found): $key"
    }
}
Write-Output "DONE. Reboot or restart Vulkan loader."

@echo off
setlocal
for /f "tokens=1* delims==" %%v in ('set VK_ 2^>nul') do set %%v=
for /f "tokens=1* delims==" %%v in ('set VULKAN_ 2^>nul') do set %%v=
set VK_ICD_FILENAMES=C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_icd.json
set VK_LOADER_DRIVERS_SELECT=amdbc250_icd.json
set VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_api_dump
set VK_APIDUMP_DETAILED=1
"F:\VulkanSDK\1.4.357.0\Bin\vulkaninfoSDK.exe" --summary > "%~dp0\vkinfo-apidump.log" 2>&1
echo TEST_EXIT=%errorlevel%
endlocal

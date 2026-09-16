@echo off
setlocal
for /f "tokens=1* delims==" %%v in ('set VK_ 2^>nul') do set %%v=
for /f "tokens=1* delims==" %%v in ('set VULKAN_ 2^>nul') do set %%v=
set VK_ICD_FILENAMES=C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_icd.json
set VK_LOADER_LAYERS_DISABLE=~all~
set VK_LOADER_DRIVERS_SELECT=amdbc250_icd.json
set VK_LOADER_DEBUG=all
"F:\VulkanSDK\1.4.357.0\Bin\vulkaninfoSDK.exe" --summary > "%~dp0\vkinfo-loader.log" 2>&1
echo TEST_EXIT=%errorlevel%
endlocal

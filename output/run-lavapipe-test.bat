@echo off
setlocal
for /f "tokens=1* delims==" %%v in ('set VK_ 2^>nul') do set %%v=
for /f "tokens=1* delims==" %%v in ('set VULKAN_ 2^>nul') do set %%v=
set VK_ICD_FILENAMES=C:\mesa\lvp_icd.x86_64.json
set VK_LOADER_LAYERS_DISABLE=~all~
set VK_LOADER_DRIVERS_SELECT=lvp_icd.x86_64.json
"F:\VulkanSDK\1.4.357.0\Bin\vulkaninfoSDK.exe" --summary
echo TEST_EXIT=%errorlevel%
endlocal

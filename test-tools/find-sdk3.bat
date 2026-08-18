@echo off
reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v KitsRoot10 2>nul
echo ---
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v KitsRoot10 2^>nul') do set KITS=%%b
echo KITS=%KITS%
if "%KITS%"=="" (echo No registry key) else (dir "%KITS%Include" /b)
echo ---
dir "C:\Program Files (x86)\Windows Kits\10\Include" /b 2>nul
dir "C:\Program Files (x86)\Windows Kits\10\Lib" /b 2>nul
echo ---
where cl

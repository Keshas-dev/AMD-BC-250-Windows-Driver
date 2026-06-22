@echo off
pnputil /add-driver "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_dream.inf" /install /force
echo.
echo Checking device status...
pnputil /enum-devices /class Display
echo.
echo Attempting device update...
pnputil /scan-devices

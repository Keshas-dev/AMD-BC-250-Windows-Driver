@echo off
setlocal enabledelayedexpansion
call "E:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul 2>&1
set "EXTRA_INC=E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;E:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt"
set "EXTRA_LIB=E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;E:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
set "INCLUDE=!EXTRA_INC!;!INCLUDE!"
set "LIB=!EXTRA_LIB!;!LIB!"
cl.exe /nologo /W3 /O2 /I "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc" "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-pci-scan.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-pci-scan.exe" advapi32.lib kernel32.lib
cl.exe /nologo /W3 /O2 /I "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc" "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-pci-discover.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-pci-discover.exe" advapi32.lib kernel32.lib
cl.exe /nologo /W3 /O2 /I "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc" "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-mmio-scan-ext.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-mmio-scan-ext.exe" advapi32.lib kernel32.lib
cl.exe /nologo /W3 /O2 /I "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc" "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-resource-discover.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-resource-discover.exe" advapi32.lib kernel32.lib setupapi.lib
cl.exe /nologo /W3 /O2 /I "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc" "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-parse-bootconfig.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-parse-bootconfig.exe" advapi32.lib kernel32.lib
cl.exe /nologo /W3 /O2 /I "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc" "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-force-enable.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-force-enable.exe" advapi32.lib kernel32.lib
cl.exe /nologo /W3 /O2 /I "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc" "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-nbio-scan.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-nbio-scan.exe" advapi32.lib kernel32.lib
cl.exe /nologo /W3 /O2 "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-acpi-tables.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-acpi-tables.exe" advapi32.lib kernel32.lib
cl.exe /nologo /W3 /O2 "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-psp-pci-config.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-psp-pci-config.exe" advapi32.lib kernel32.lib
cl.exe /nologo /W3 /O2 "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\test-host-bridge.c" /Fe"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\test-host-bridge.exe" advapi32.lib kernel32.lib

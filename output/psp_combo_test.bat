@echo off
echo === PSP Combo Test === > C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log
cd C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\output
echo Step 1: INIT_HW... >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log
test-psp-driver.exe -i 0xFE800000 0x200000 >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log 2>&1
echo Step 2: LOAD_FW... >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log
test-psp-driver.exe -f combo_psp.bin >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log 2>&1
echo Step 3: SEND_CMD 4... >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log
test-psp-driver.exe -C 4 >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log 2>&1
echo Step 4: SEND_CMD 8... >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log
test-psp-driver.exe -C 8 >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log 2>&1
echo Step 5: STATUS... >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log
test-psp-driver.exe -s >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log 2>&1
echo === Done === >> C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\psp-combo-test.log

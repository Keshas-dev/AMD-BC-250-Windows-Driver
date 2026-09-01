# ========================================================
# PSP SOS Firmware Injection for BC-250 (Cyan Skillfish 2)
# Injects firmware address and LOAD_SOS command
# ========================================================
# IMPORTANT: Run test_psp.nsh FIRST to verify register access!

echo -off
cls
echo "========================================================"
echo "               PSP SOS Firmware Injection"
echo "========================================================"
echo ""

echo "Step 1: Checking current SOS status (C2PMSG_81)..."
echo "Address: FE858244"
mm FE858244 -w 4 -n
echo ""

echo "Step 2: Writing firmware address to C2PMSG_36..."
echo "Address: FE858190"
echo "NOTE: Value 7E4 assumes firmware is at 7E400000 (>> 20)"
mm FE858190 7E4 -w 4 -n
echo ""

echo "Step 3: Verifying C2PMSG_36 write success..."
mm FE858190 -w 4 -n
echo ""

echo "Step 4: Sending LOAD_SOS command (20000000) to C2PMSG_35..."
echo "Address: FE85818C"
mm FE85818C 20000000 -w 4 -n
echo ""

echo "Step 5: Waiting 1 second for PSP to process..."
stall 1000000
echo "Done."
echo ""

echo "Step 6: Checking SOS status after injection (C2PMSG_81)..."
mm FE858244 -w 4 -n
echo ""

echo "========================================================"
echo "                     ANALYSIS                           "
echo "========================================================"
echo " Check the C2PMSG_81 value from Step 6:"
echo ""
echo " -> If bit 31 is set (e.g., 0x8XXXXXXX):"
echo "    SOS IS ALIVE! NBIO should now be unlocked."
echo "    You can type 'exit' or boot Windows normally."
echo ""
echo " -> If bit 31 is NOT set (e.g., 0x00XXXXXX):"
echo "    SOS did not start. Double-check your firmware address"
echo "    alignment (>> 20) in RAM and try again."
echo "========================================================"
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

# --- STEP 1 ---
echo "Step 1: Checking current SOS status (C2PMSG_81)..."
echo "Address: 0xFE858244"
mm 0xFE858244 -mm -b 4
echo ""

# --- STEP 2 ---
echo "Step 2: Writing firmware address to C2PMSG_36..."
echo "Address: 0xFE858190"
echo "IMPORTANT: Replace 0x000007E4 with actual TMR addr>>20 from Windows test!"
echo "           Run 'test-psp-loader.exe' on Windows, look for 'TMR allocated:'"
echo "           Example: if TMR=0x7E512000, addr>>20 = 0x000007E5"
mm 0xFE858190 0x000007E4 -mm -b 4
echo ""

# --- STEP 3 ---
echo "Step 3: Verifying C2PMSG_36 write success..."
mm 0xFE858190 -mm -b 4
echo ""

# --- STEP 4 ---
echo "Step 4: Sending LOAD_SOS command (0x20000000) to C2PMSG_35..."
echo "Address: 0xFE85818C"
mm 0xFE85818C 0x20000000 -mm -b 4
echo ""

# --- STEP 5 ---
echo "Step 5: Waiting 1 second for PSP to process..."
stall 1000000
echo "Done."
echo ""

# --- STEP 6 ---
echo "Step 6: Checking SOS status after injection (C2PMSG_81)..."
mm 0xFE858244 -mm -b 4
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

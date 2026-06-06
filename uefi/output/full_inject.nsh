# ========================================================
# PSP Full Injection for BC-250 (Cyan Skillfish 2)
# Full sequence: Test, Inject, Verify
# ========================================================

echo -off
cls
echo "========================================================"
echo "          PSP Full Injection Sequence"
echo "========================================================"
echo ""

# --- STEP 1: Test registers ---
echo "Step 1: Testing register access..."
echo ""

echo "Reading C2PMSG_81 (Sign of Life) at 0xFE858244:"
mm 0xFE858244 -mm -b 4
echo ""

echo "Reading C2PMSG_35 (CMD) at 0xFE85818C:"
mm 0xFE85818C -mm -b 4
echo ""

echo "Reading C2PMSG_36 (ADDR) at 0xFE858190:"
mm 0xFE858190 -mm -b 4
echo ""

# --- STEP 2: Write firmware address ---
echo "Step 2: Writing firmware address to C2PMSG_36..."
echo "IMPORTANT: Replace 0x000007E4 with actual TMR addr>>20 from Windows test!"
mm 0xFE858190 0x000007E4 -mm -b 4
echo ""

# --- STEP 3: Verify write ---
echo "Step 3: Verifying C2PMSG_36..."
mm 0xFE858190 -mm -b 4
echo ""

# --- STEP 4: Send LOAD_SOS ---
echo "Step 4: Sending LOAD_SOS command (0x20000000)..."
mm 0xFE85818C 0x20000000 -mm -b 4
echo ""

# --- STEP 5: Wait ---
echo "Step 5: Waiting 2 seconds for PSP..."
stall 2000000
echo ""

# --- STEP 6: Check result ---
echo "Step 6: Checking SOS status..."
mm 0xFE858244 -mm -b 4
echo ""

echo "========================================================"
echo "                     RESULT                             "
echo "========================================================"
echo ""
echo " Check C2PMSG_81 value above:"
echo ""
echo " 0x8XXXXXXX = SOS ALIVE! Boot Windows now."
echo " 0x00XXXXXX = SOS not started. Check firmware address."
echo ""
echo "========================================================"

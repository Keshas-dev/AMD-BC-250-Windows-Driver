# ========================================================
# PSP Register Test for BC-250 (Cyan Skillfish 2)
# Tests if C2PMSG registers are accessible at EFI boot time
# ========================================================

echo -off
cls
echo "========================================================"
echo "          PSP Register Test (EFI Boot Time)"
echo "========================================================"
echo ""

# --- STEP 1 ---
echo "Step 1: Reading Sign of Life (C2PMSG_81)..."
echo "Address: 0xFE858244"
mm 0xFE858244 -mm -b 4
echo ""

# --- STEP 2 ---
echo "Step 2: Reading C2PMSG_35 (CMD - Locked Check)..."
echo "Address: 0xFE85818C"
mm 0xFE85818C -mm -b 4
echo ""

# --- STEP 3 ---
echo "Step 3: Reading C2PMSG_36 (ADDR)..."
echo "Address: 0xFE858190"
mm 0xFE858190 -mm -b 4
echo ""

# --- STEP 4 ---
echo "Step 4: Reading C2PMSG_64 (Mailbox)..."
echo "Address: 0xFE858200"
mm 0xFE858200 -mm -b 4
echo ""

# --- STEP 5 ---
echo "Step 5: Reading GRBM_STATUS (GPU Core Lock Check)..."
echo "Address: 0xFE802004"
mm 0xFE802004 -mm -b 4
echo ""

echo "========================================================"
echo "                     ANALYSIS                           "
echo "========================================================"
echo " -> If C2PMSG_35/36 show real values (NOT 0xFFFFFFFF):"
echo "    NBIO is OPEN at EFI boot time - injection will work!"
echo "    Run inject_psp.nsh next."
echo ""
echo " -> If C2PMSG_35/36 show 0xFFFFFFFF:"
echo "    NBIO is already locked - try disabling NBIO DEV in BIOS"
echo "    (Advanced > AMD CBS > NBIO > Device Exclusion Vector)"
echo ""
echo " -> If C2PMSG_81 shows a value like 0x005XXXXX:"
echo "    PSP Bootloader is alive and waiting."
echo "========================================================"

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

echo "Step 1: Reading Sign of Life (C2PMSG_81)..."
echo "Address: FE858244"
mm FE858244 -w 4 -n
echo ""

echo "Step 2: Reading C2PMSG_35 (CMD - Locked Check)..."
echo "Address: FE85818C"
mm FE85818C -w 4 -n
echo ""

echo "Step 3: Reading C2PMSG_36 (ADDR)..."
echo "Address: FE858190"
mm FE858190 -w 4 -n
echo ""

echo "Step 4: Reading C2PMSG_64 (Mailbox)..."
echo "Address: FE858200"
mm FE858200 -w 4 -n
echo ""

echo "Step 5: Reading GRBM_STATUS (GPU Core Lock Check)..."
echo "Address: FE802004"
mm FE802004 -w 4 -n
echo ""

echo "========================================================"
echo "                     ANALYSIS                           "
echo "========================================================"
echo " -> If C2PMSG_35/36 show real values (NOT 0xFFFFFFFF):"
echo "    NBIO is OPEN at EFI boot time - injection will work!"
echo "    You can run your inject_psp.nsh next."
echo ""
echo " -> If C2PMSG_35/36 show 0xFFFFFFFF:"
echo "    NBIO is already locked by UEFI - try changing BIOS:"
echo "    (Advanced > AMD CBS > NBIO > Device Exclusion Vector)"
echo ""
echo " -> If C2PMSG_81 shows a value like 0x005XXXXX:"
echo "    PSP Bootloader is alive and waiting."
echo "========================================================"
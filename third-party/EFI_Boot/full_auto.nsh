# ========================================================
# Full Auto WGP Unlock for BC-250 (Cyan Skillfish 2)
# Runs all scripts in sequence, then boots Windows
# ========================================================

echo -off
cls
echo "========================================================"
echo "     BC-250 WGP Unlock - Full Auto Sequence"
echo "========================================================"
echo ""

# Step 1: Test PSP registers
echo ">>> Step 1: Testing PSP register access..."
psp\test_psp.nsh
echo ""
echo ">>> Step 1 done. Wait 2s..."
stall 2000000
echo ""

# Step 2: Inject PSP firmware (SOS)
echo ">>> Step 2: Injecting PSP firmware (SOS)..."
psp\inject_psp.nsh
echo ""
echo ">>> Step 2 done. Wait 2s..."
stall 2000000
echo ""

# Step 3: Unlock WGP
echo ">>> Step 3: Unlocking WGP..."
psp\WGP_unlock.nsh
echo ""
stall 2000000

# Step 4: Boot Windows
echo "Booting Windows..."
echo ""
echo "Exiting EFI Shell in 3s..."
stall 3000000
exit

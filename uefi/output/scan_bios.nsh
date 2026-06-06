# ========================================================
# BIOS Setup Scanner for BC-250
# Scans all UEFI Setup variables to find NBIO/DEV offsets
# 
# Usage in UEFI Shell:
#   setup_var.efi          - list all variables
#   setup_var.efi 0x100 0x00 - set variable
# ========================================================

echo -off
cls
echo "========================================================"
echo "     BC-250 BIOS Setup Variable Scanner"
echo "========================================================"
echo ""
echo "Step 1: List all NVRAM Setup variables..."
echo ""
echo "Run: setup_var.efi"
echo ""
echo "Step 2: Look for these settings in the output:"
echo "  - Device Exclusion Vector"
echo "  - IOMMU / AMD-Vi"
echo "  - PSP Support"
echo "  - Above 4G Decoding"
echo "  - Resizable BAR"
echo ""
echo "Step 3: Note the VarOffset values and run:"
echo "  setup_var.efi 0xYYYY 0x00    (disable)"
echo "  setup_var.efi 0xYYYY 0x01    (enable)"
echo ""
echo "========================================================"
echo "     KNOWN OFFSETS (Common AMI BIOS)"
echo "========================================================"
echo ""
echo "  IOMMU:       0x1A2 - 0x1A5  Disable=0x00"
echo "  PSP Support: 0x4F0 - 0x4F5  Disable=0x00"
echo "  DEV Vector:  0x500+          Disable=0x00"
echo "  Above 4G:    0x???           Enable=0x01"
echo ""
echo "========================================================"
echo "Run: setup_var.efi to see all variables"
echo "========================================================"

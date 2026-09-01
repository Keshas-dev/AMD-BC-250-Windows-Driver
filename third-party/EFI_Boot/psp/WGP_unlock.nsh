# ========================================================
# WGP Unlock for BC-250 (Cyan Skillfish 2)  v2 - DWORD FIX
# FIX 2026-08-21: mm defaults to 1-BYTE width! Old script's
#   values >FF failed ("Invalid argument") and even byte
#   writes didn't stick. All ops now use "-w 4" (dword).
# WARNING: Run inject_psp.nsh FIRST to load SOS!
# ========================================================
#
# Register Map (BAR5 = 0xFE800000, verified via pci dump:
#   CMD=0x0006 MEM+BUSMASTER ON, BAR5 assigned):
#   SPI_PG_ENABLE_STATIC_WGP_MASK  = 0x5C3C
#   CC_GC_SHADER_ARRAY_CONFIG      = 0x9C1C
#   RLC_PG_ALWAYS_ON_WGP_MASK      = 0x3D64
#   GRBM_GFX_INDEX                 = 0x34D0
#
# Values (duggasco 40CU unlock method):
#   CC  = 0x00000000 (clear harvest mask)
#   SPI = 0x0000001F (enable all 5 WGPs)
#   RLC = 0x0000001F (keep all WGPs powered)
#
# GRBM Bank Selects (Linux gfx10 layout, HEX):
#   SE0/SH0 = 0
#   SE0/SH1 = 100
#   SE1/SH0 = 10000
#   SE1/SH1 = 10100
#   Broadcast = 15000000
# ========================================================

echo -off
cls
echo "========================================================"
echo "     WGP Unlock for BC-250 (EFI Boot) v2 DWORD"
echo "========================================================"
echo ""
echo "BAR5 Physical Base: 0xFE800000"
echo ""

echo "Step 1: Verifying BAR5 access - GPU_ID dword (expect ...9700):"
mm FE800000 -w 4 -n
echo ""

echo "Step 2: Current values (dwords):"
echo "GRBM_GFX_INDEX:"
mm FE8034D0 -w 4
echo "SPI_PG:"
mm FE805C3C -w 4
echo "CC_ARRAY:"
mm FE809C1C -w 4
echo "RLC_PG:"
mm FE803D64 -w 4
echo ""

echo "Step 3: Unlocking WGP for all shader array banks..."
echo ""

echo "  Bank 0 (SE0/SH0)..."
mm FE8034D0 00000000 -w 4 -n
mm FE809C1C 00000000 -w 4 -n
mm FE805C3C 0000001F -w 4 -n
mm FE803D64 0000001F -w 4 -n
echo ""

echo "  Bank 1 (SE0/SH1)..."
mm FE8034D0 00000100 -w 4 -n
mm FE809C1C 00000000 -w 4 -n
mm FE805C3C 0000001F -w 4 -n
mm FE803D64 0000001F -w 4 -n
echo ""

echo "  Bank 2 (SE1/SH0)..."
mm FE8034D0 00010000 -w 4 -n
mm FE809C1C 00000000 -w 4 -n
mm FE805C3C 0000001F -w 4 -n
mm FE803D64 0000001F -w 4 -n
echo ""

echo "  Bank 3 (SE1/SH1)..."
mm FE8034D0 00010100 -w 4 -n
mm FE809C1C 00000000 -w 4 -n
mm FE805C3C 0000001F -w 4 -n
mm FE803D64 0000001F -w 4 -n
echo ""

echo "Step 4: Restoring broadcast select..."
mm FE8034D0 15000000 -w 4 -n
echo ""

echo "Step 5: Verify PER-BANK (SE0/SH0 first)..."
mm FE8034D0 00000000 -w 4 -n
echo "SPI_PG (expect 1F):"
mm FE805C3C -w 4
echo "CC_ARRAY (expect 0):"
mm FE809C1C -w 4
echo "RLC_PG (expect 1F):"
mm FE803D64 -w 4
echo ""

echo "Step 6: Restore broadcast and verify again..."
mm FE8034D0 15000000 -w 4 -n
echo "SPI_PG broadcast (expect 1F):"
mm FE805C3C -w 4
echo "CC_ARRAY broadcast:"
mm FE809C1C -w 4
echo "RLC_PG broadcast:"
mm FE803D64 -w 4
echo ""

echo "========================================================"
echo "                     ANALYSIS"
echo "========================================================"
echo ""
echo "If per-bank SPI_PG reads 0000001F:"
echo "  WGP UNLOCK SUCCESSFUL! Boot Windows and run"
echo "  output\wgp-persist-check.exe to confirm persistence."
echo ""
echo "If SPI_PG still reads 00000000:"
echo "  Writes rejected even at EFI. Try inject_psp.nsh first,"
echo "  or the NBIO gate needs a BIOS setting change."
echo "========================================================"

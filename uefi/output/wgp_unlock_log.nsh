# wgp_unlock_log.nsh — BC-250 GPU WGP Unlock su detaliu log
# Visa informacija irasoma i wgp_log.txt

# Initialize log
echo "=== BC-250 GPU WGP Unlock Log ===" > wgp_log.txt
echo "Date: 2026-08-07" >> wgp_log.txt
echo "================================================" >> wgp_log.txt

# GPU register addresses (BAR5 physical = 0xFE800000)
set GPU_ID_ADDR     0xFE800000
set GRBM_STATUS     0xFE803260
set SPI_PG_ADDR     0xFE805C3C
set RLC_PG_ADDR     0xFE803D64
set CC_ARRAY_ADDR   0xFE809C1C
set C2PMSG_81       0xFE805824

echo "" >> wgp_log.txt
echo "=== STEP 1: Test basic access ===" >> wgp_log.txt
echo "Testing GPU_ID at 0xFE800000 (expect 0x9FFF9700):" >> wgp_log.txt
mm $GPU_ID_ADDR -mm -b 4 >> wgp_log.txt 2>&1

echo "" >> wgp_log.txt
echo "=== STEP 2: Check SOS status ===" >> wgp_log.txt
echo "Reading C2PMSG_81 (SOS) at 0xFE805824 (expect 0xF0000010 if alive):" >> wgp_log.txt
mm $C2PMSG_81 -mm -b 4 >> wgp_log.txt 2>&1

echo "" >> wgp_log.txt
echo "=== STEP 3: Read baseline values ===" >> wgp_log.txt
echo "GRBM_STATUS:" >> wgp_log.txt
mm $GRBM_STATUS -mm -b 4 >> wgp_log.txt 2>&1
echo "SPI_PG:" >> wgp_log.txt
mm $SPI_PG_ADDR -mm -b 4 >> wgp_log.txt 2>&1
echo "RLC_PG:" >> wgp_log.txt
mm $RLC_PG_ADDR -mm -b 4 >> wgp_log.txt 2>&1
echo "CC_ARRAY:" >> wgp_log.txt
mm $CC_ARRAY_ADDR -mm -b 4 >> wgp_log.txt 2>&1

echo "" >> wgp_log.txt
echo "=== STEP 4: Try WGP unlock write ===" >> wgp_log.txt
echo "Writing SPI_PG = 0x1F (WGP0-4):" >> wgp_log.txt
mm $SPI_PG_ADDR 0x0000001F -mm -b 4 >> wgp_log.txt 2>&1
echo "Writing RLC_PG = 0x1F:" >> wgp_log.txt
mm $RLC_PG_ADDR 0x0000001F -mm -b 4 >> wgp_log.txt 2>&1
echo "Writing CC_ARRAY = 0xFFE00000:" >> wgp_log.txt
mm $CC_ARRAY_ADDR 0xFFE00000 -mm -b 4 >> wgp_log.txt 2>&1

echo "" >> wgp_log.txt
echo "=== STEP 5: Verify readback ===" >> wgp_log.txt
echo "SPI_PG readback (expect 0x0000001F if success):" >> wgp_log.txt
mm $SPI_PG_ADDR -mm -b 4 >> wgp_log.txt 2>&1
echo "RLC_PG readback:" >> wgp_log.txt
mm $RLC_PG_ADDR -mm -b 4 >> wgp_log.txt 2>&1
echo "CC_ARRAY readback:" >> wgp_log.txt
mm $CC_ARRAY_ADDR -mm -b 4 >> wgp_log.txt 2>&1

echo "" >> wgp_log.txt
echo "=== STEP 6: Alternative SPI_PG offsets ===" >> wgp_log.txt
echo "Trying 0xFE8034FC (SPI_WGP_MASK):" >> wgp_log.txt
mm 0xFE8034FC -mm -b 4 >> wgp_log.txt 2>&1
echo "Write 0x1F to 0xFE8034FC:" >> wgp_log.txt
mm 0xFE8034FC 0x0000001F -mm -b 4 >> wgp_log.txt 2>&1
echo "Readback:" >> wgp_log.txt
mm 0xFE8034FC -mm -b 4 >> wgp_log.txt 2>&1

echo "" >> wgp_log.txt
echo "=== STEP 7: Wait 2 seconds and recheck ===" >> wgp_log.txt
stall 2000000
echo "SPI_PG after 2s:" >> wgp_log.txt
mm $SPI_PG_ADDR -mm -b 4 >> wgp_log.txt 2>&1

echo "" >> wgp_log.txt
echo "=== Log Complete ===" >> wgp_log.txt

# Also display to screen
echo ""
echo "=== RESULTS ==="
type wgp_log.txt
echo ""
echo "Log saved to wgp_log.txt"
echo "Copy this file to USB after boot to Windows for analysis"

# wgp_unlock.nsh — BC-250 GPU WGP Unlock via UEFI Shell
# Bands rašyti SPI_PG = 0x1F per UEFI Boot Services

echo "=== BC-250 GPU WGP Unlock Test ==="

# GPU BAR5 physical base = 0xFE800000
# SPI_PG_ENABLE_STATIC_WGP_MASK = 0x5C3C
# RLC_PG_ALWAYS_ON_WGP_MASK = 0x3D64
# CC_GC_SHADER_ARRAY_CONFIG = 0x9C1C

set SPI_PG_ADDR 0xFE805C3C
set RLC_PG_ADDR 0xFE803D64
set CC_ARRAY_ADDR 0xFE809C1C

# Read baseline
echo "Reading baseline..."
mm $SPI_PG_ADDR -mm -b 4
mm $RLC_PG_ADDR -mm -b 4

# Write unlock values
echo "Writing unlock values..."
mm $SPI_PG_ADDR 0x0000001F -mm -b 4
mm $RLC_PG_ADDR 0x0000001F -mm -b 4
mm $CC_ARRAY_ADDR 0xFFE00000 -mm -b 4

# Read back
echo "Reading back..."
mm $SPI_PG_ADDR -mm -b 4
mm $RLC_PG_ADDR -mm -b 4
mm $CC_ARRAY_ADDR -mm -b 4

echo "=== Done ==="

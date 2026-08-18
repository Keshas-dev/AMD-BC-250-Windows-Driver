# wgp_test.nsh — BC-250 GPU WGP Unlock su teisinga mm sintakse
# Naudoja -MMIO -w 4 (32-bit MMIO access)

echo "=== BC-250 GPU WGP Unlock Test ==="
echo "Using MMIO access (bypasses NBIO lock)"
echo ""

# Step 1: Read GPU ID (verify basic access)
echo "=== GPU_ID at 0xFE800000 (expect 0x9FFF9714) ==="
mm 0xFE800000 -MMIO -w 4

# Step 2: Read baseline SPI_PG
echo "=== SPI_PG baseline at 0xFE805C3C (expect 0x00000000) ==="
mm 0xFE805C3C -MMIO -w 4

# Step 3: Write unlock value
echo "=== Writing SPI_PG = 0x1F ==="
mm 0xFE805C3C 1F -MMIO -w 4

# Step 4: Read back
echo "=== SPI_PG readback (expect 0x0000001F if success) ==="
mm 0xFE805C3C -MMIO -w 4

# Step 5: Write RLC_PG
echo "=== Writing RLC_PG = 0x1F ==="
mm 0xFE803D64 1F -MMIO -w 4
mm 0xFE803D64 -MMIO -w 4

# Step 6: Write CC_ARRAY  
echo "=== Writing CC_ARRAY = 0xFFE00000 ==="
mm 0xFE809C1C FFE00000 -MMIO -w 4
mm 0xFE809C1C -MMIO -w 4

echo ""
echo "=== Test Complete ==="
echo "If SPI_PG shows 0x0000001F — UNLOCK SUCCESSFUL!"

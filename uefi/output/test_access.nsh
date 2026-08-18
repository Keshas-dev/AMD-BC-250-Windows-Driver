# test_access.nsh — Test if SPI_PG address is accessible via UEFI Shell

echo "=== Testing GPU Register Access ==="

# Test 1: Read GPU_ID (should be 0x9FFF9700)
echo "Test 1: GPU_ID at 0xFE800000"
mm 0xFE800000 -mm -b 4

# Test 2: Read SPI_PG (should be 0x00000000)
echo "Test 2: SPI_PG at 0xFE805C3C"
mm 0xFE805C3C -mm -b 4

# Test 3: Try write SPI_PG = 0x1F
echo "Test 3: Write SPI_PG = 0x1F"
mm 0xFE805C3C 0x0000001F -mm -b 4

# Test 4: Read back
echo "Test 4: Read back SPI_PG"
mm 0xFE805C3C -mm -b 4

echo "=== Test Complete ==="

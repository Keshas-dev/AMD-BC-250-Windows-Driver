#!/bin/bash
# Warm Reboot Test - Tests if Windows activates NBIO firewall
# Run this AFTER booting Windows first, then warm rebooting to Linux

OUTPUT_DIR="$HOME/bc250-warm-reboot-test"
mkdir -p "$OUTPUT_DIR"

echo "=== WARM REBOOT TEST ==="
echo "This test checks if Windows activates the NBIO firewall"
echo "Output: $OUTPUT_DIR"
echo ""

# 1. System info
echo "[1/5] System info..."
uname -a > "$OUTPUT_DIR/uname.txt" 2>&1
date > "$OUTPUT_DIR/date.txt" 2>&1

# 2. PSP status
echo "[2/5] PSP status..."
dmesg | grep -i psp > "$OUTPUT_DIR/dmesg-psp.txt" 2>&1
dmesg | grep -i firmware > "$OUTPUT_DIR/dmesg-firmware.txt" 2>&1

# 3. MMIO access test
echo "[3/5] MMIO access test..."
cat /sys/kernel/debug/dri/0/amdgpu_regs > "$OUTPUT_DIR/amdgpu-regs.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_psp_sos > "$OUTPUT_DIR/amdgpu-psp-sos.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_psp_tmr > "$OUTPUT_DIR/amdgpu-psp-tmr.txt" 2>&1

# 4. NBIO status
echo "[4/5] NBIO status..."
dmesg | grep -i nbio > "$OUTPUT_DIR/dmesg-nbio.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_nbio > "$OUTPUT_DIR/amdgpu-nbio.txt" 2>&1

# 5. Full dmesg
echo "[5/5] Full dmesg..."
dmesg | grep -i -E "amdgpu|psp|nbio|firmware|smn|mmio|lock" > "$OUTPUT_DIR/dmesg-full.txt" 2>&1

echo ""
echo "=== TEST COMPLETE ==="
echo "Output: $OUTPUT_DIR"
echo ""
echo "COMPARE WITH COLD BOOT RESULTS:"
echo "- If PSP works: Windows did NOT activate firewall (Scenario A)"
echo "- If PSP fails: Windows DID activate firewall (Scenario B)"
echo ""
ls -la "$OUTPUT_DIR"
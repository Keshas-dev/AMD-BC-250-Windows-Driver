#!/bin/bash
# AMD BC-250 Linux amdgpu info collection script
# Runs on CachyOS and collects GPU/PSP/NBIO info

OUTPUT_DIR="./bc250-linux-info"
mkdir -p "$OUTPUT_DIR"

echo "=== AMD BC-250 Linux amdgpu Info Collection ==="
echo "Output: $OUTPUT_DIR"
echo ""

# 1. System info
echo "[1/10] System info..."
uname -a > "$OUTPUT_DIR/uname.txt" 2>&1
lsb_release -a >> "$OUTPUT_DIR/uname.txt" 2>&1
cat /proc/version >> "$OUTPUT_DIR/uname.txt" 2>&1

# 2. GPU info
echo "[2/10] GPU info..."
lspci -v | grep -A 30 "VGA" > "$OUTPUT_DIR/lspci-vga.txt" 2>&1
lspci -nn | grep -i amd > "$OUTPUT_DIR/lspci-amd.txt" 2>&1
cat /sys/class/drm/card0/device/vendor > "$OUTPUT_DIR/gpu-vendor.txt" 2>&1
cat /sys/class/drm/card0/device/device > "$OUTPUT_DIR/gpu-device.txt" 2>&1
cat /sys/class/drm/card0/device/subsystem_vendor >> "$OUTPUT_DIR/gpu-vendor.txt" 2>&1
cat /sys/class/drm/card0/device/subsystem_device >> "$OUTPUT_DIR/gpu-device.txt" 2>&1

# 3. PSP info
echo "[3/10] PSP info..."
dmesg | grep -i psp > "$OUTPUT_DIR/dmesg-psp.txt" 2>&1
dmesg | grep -i firmware > "$OUTPUT_DIR/dmesg-firmware.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_firmware_info > "$OUTPUT_DIR/amdgpu-firmware-info.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_psp_sos > "$OUTPUT_DIR/amdgpu-psp-sos.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_psp_tmr > "$OUTPUT_DIR/amdgpu-psp-tmr.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_psp_asd > "$OUTPUT_DIR/amdgpu-psp-asd.txt" 2>&1

# 4. MMIO registers
echo "[4/10] MMIO registers..."
cat /sys/kernel/debug/dri/0/amdgpu_regs > "$OUTPUT_DIR/amdgpu-regs.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_regs_didt > "$OUTPUT_DIR/amdgpu-regs-didt.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_regs_pcie > "$OUTPUT_DIR/amdgpu-regs-pcie.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_regs_smc > "$OUTPUT_DIR/amdgpu-regs-smc.txt" 2>&1

# 5. NBIO info
echo "[5/10] NBIO info..."
dmesg | grep -i nbio > "$OUTPUT_DIR/dmesg-nbio.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_nbio > "$OUTPUT_DIR/amdgpu-nbio.txt" 2>&1

# 6. SMN info
echo "[6/10] SMN info..."
cat /sys/kernel/debug/dri/0/amdgpu_smn > "$OUTPUT_DIR/amdgpu-smn.txt" 2>&1

# 7. Firmware files
echo "[7/10] Firmware files..."
ls -la /lib/firmware/amdgpu/ > "$OUTPUT_DIR/firmware-list.txt" 2>&1
ls -la /lib/firmware/amdgpu/navi10_*.bin >> "$OUTPUT_DIR/firmware-navi10.txt" 2>&1
ls -la /lib/firmware/amdgpu/cyan_skillfish2_*.bin >> "$OUTPUT_DIR/firmware-cyan.txt" 2>&1
md5sum /lib/firmware/amdgpu/navi10_*.bin >> "$OUTPUT_DIR/firmware-navi10-md5.txt" 2>&1

# 8. Ring buffer info
echo "[8/10] Ring buffer info..."
cat /sys/kernel/debug/dri/0/amdgpu_ring_gfx > "$OUTPUT_DIR/amdgpu-ring-gfx.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_ring_comp > "$OUTPUT_DIR/amdgpu-ring-comp.txt" 2>&1
cat /sys/kernel/debug/dri/0/amdgpu_ring_sdma > "$OUTPUT_DIR/amdgpu-ring-sdma.txt" 2>&1

# 9. GPU status
echo "[9/10] GPU status..."
cat /sys/kernel/debug/dri/0/amdgpu_gpu_recover > "$OUTPUT_DIR/amdgpu-gpu-recover.txt" 2>&1
cat /sys/class/drm/card0/device/power_dpm_state > "$OUTPUT_DIR/amdgpu-dpm-state.txt" 2>&1
cat /sys/class/drm/card0/device/power_dpm_force_performance_level > "$OUTPUT_DIR/amdgpu-dpm-level.txt" 2>&1

# 10. Full dmesg
echo "[10/10] Full dmesg..."
dmesg | grep -i -E "amdgpu|psp|nbio|firmware|smn|mmio" > "$OUTPUT_DIR/dmesg-full.txt" 2>&1

echo ""
echo "=== Collection complete ==="
echo "Output: $OUTPUT_DIR"
echo ""
ls -la "$OUTPUT_DIR"

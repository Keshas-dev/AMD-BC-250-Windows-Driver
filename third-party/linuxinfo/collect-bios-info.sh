#!/bin/bash
# BC-250 BIOS/PSP diagnostic collector for CachyOS
# Paleisti: sudo bash collect-bios-info.sh
# Rezultatai: bc250-bios-info/ kataloge

OUTDIR="bc250-bios-info"
mkdir -p "$OUTDIR"
cd "$OUTDIR"

echo "=== BC-250 BIOS/PSP Diagnostic Collection ===" | tee _summary.txt
echo "Date: $(date)" | tee -a _summary.txt
echo "" | tee -a _summary.txt

# === 1. Basic system info ===
echo "=== 1. System info ===" | tee -a _summary.txt
uname -a > uname.txt 2>&1
cat /proc/version >> uname.txt 2>&1
lsb_release -a > lsb.txt 2>&1
cat /sys/class/drm/card1/device/vendor 2>/dev/null || echo "N/A" > gpu-vendor.txt
cat /sys/class/drm/card1/device/device 2>/dev/null || echo "N/A" > gpu-device.txt
cat /sys/class/drm/card1/device/subsystem_vendor 2>/dev/null || echo "N/A" > gpu-svendor.txt
cat /sys/class/drm/card1/device/subsystem_device 2>/dev/null || echo "N/A" > gpu-sdevice.txt

# === 2. Full lspci for all GPU functions ===
echo "=== 2. PCI topology ===" | tee -a _summary.txt
lspci -nn | grep -E "13fe|13ff|143e|1002|1022" > lspci-gpu.txt 2>&1
lspci -vv -s 01:00.0 > lspci-f0.txt 2>&1
lspci -vv -s 01:00.1 > lspci-f1.txt 2>&1
lspci -vv -s 01:00.2 > lspci-f2.txt 2>&1
lspci -vvvs 01:00.0 2>&1 | grep -i "Region\|BAR\|Memory\|size" > lspci-bars.txt 2>&1

# === 3. Full dmesg PSP/NBIO/SMU ===
echo "=== 3. Kernel messages ===" | tee -a _summary.txt
dmesg | grep -i "amdgpu\|psp\|nbio\|smu\|skillfish\|cyan" > dmesg-amdgpu.txt 2>&1
dmesg | grep -i "psp" > dmesg-psp.txt 2>&1
dmesg | grep -i "nbio" > dmesg-nbio.txt 2>&1
dmesg | grep -i "smu" > dmesg-smu.txt 2>&1
dmesg > dmesg-full.txt 2>&1

# === 4. Debugfs - try all possible paths ===
echo "=== 4. Debugfs registers ===" | tee -a _summary.txt
for card in /sys/kernel/debug/dri/*; do
    if [ -d "$card" ]; then
        echo "--- $card ---" | tee -a _summary.txt
        # Try register dumps
        for regfile in amdgpu_regs amdgpu_regs_pcie amdgpu_regs_smc amdgpu_smn amdgpu_nbio; do
            if [ -f "$card/$regfile" ]; then
                head -100 "$card/$regfile" > "debug-$(basename $card)-$regfile.txt" 2>&1
                echo "  ✅ $regfile found" | tee -a _summary.txt
            else
                echo "  ❌ $regfile not found" >> _summary.txt
            fi
        done
        # Try PSP debugfs
        for psffile in amdgpu_psp_tmr amdgpu_psp_asd amdgpu_psp_sos amdgpu_firmware_info; do
            if [ -f "$card/$psffile" ]; then
                head -100 "$card/$psffile" > "debug-$(basename $card)-$psffile.txt" 2>&1
                echo "  ✅ $psffile found" | tee -a _summary.txt
            fi
        done
    fi
done

# === 5. VBIOS dump from VFCT or ROM BAR ===
echo "=== 5. VBIOS dump ===" | tee -a _summary.txt
# Try ROM BAR via sysfs
if [ -f /sys/class/drm/card1/device/rom ]; then
    cat /sys/class/drm/card1/device/rom > vbios.bin 2>&1
    ls -la vbios.bin >> _summary.txt
    echo "VBIOS size: $(stat -c%s vbios.bin 2>/dev/null || echo 0) bytes"
    # Check for ATOM BIOS signature
    hexdump -C vbios.bin 2>/dev/null | head -5 > vbios-header.txt 2>&1
    strings vbios.bin 2>/dev/null | grep -i "ATOM\|BIOS\|113-" > vbios-strings.txt 2>&1
    echo "VBIOS strings:" | tee -a _summary.txt
    cat vbios-strings.txt | tee -a _summary.txt
else
    echo "ROM sysfs not available" | tee -a _summary.txt
    # Try via VFCT in ACPI tables
    if [ -d /sys/firmware/acpi/tables ]; then
        cp /sys/firmware/acpi/tables/VFCT vbios-vfct.bin 2>/dev/null && echo "VFCT table saved" || echo "VFCT not found"
    fi
fi

# === 6. IP Discovery table search ===
echo "=== 6. IP Discovery table ===" | tee -a _summary.txt
if [ -f vbios.bin ]; then
    # Search for IP Discovery signature "IPDIS" or "IP DIS"
    hexdump -C vbios.bin 2>/dev/null | grep -i "4950" > ip-discovery-search.txt 2>&1
    strings vbios.bin 2>/dev/null | grep -i "discovery\|harvest\|ip_block" > ip-discovery-strings.txt 2>&1
    # Search for MP0 base candidate values at various offsets
    echo "Scanning VBIOS for IP discovery signatures..." | tee -a _summary.txt
fi

# === 7. Firmware files ===
echo "=== 7. Firmware files ===" | tee -a _summary.txt
ls -la /lib/firmware/amdgpu/ | grep -i "cyan\|skillfish" > firmware-cyan.txt 2>&1
ls -la /lib/firmware/amdgpu/ | grep -i "psp\|sos\|asd\|ta\|smc" > firmware-psp.txt 2>&1
ls -la /lib/firmware/amdgpu/ > firmware-list.txt 2>&1
# Check if any PSP firmware exists at all
find /lib/firmware -name "*sos*" -o -name "*asd*" -o -name "*psp*" 2>/dev/null | head -20 > firmware-psp-all.txt 2>&1

# === 8. AMD GPU sysfs info ===
echo "=== 8. Sysfs GPU info ===" | tee -a _summary.txt
for f in /sys/class/drm/card1/device/*; do
    name=$(basename "$f")
    if [ ! -d "$f" ] && [ -r "$f" ]; then
        val=$(cat "$f" 2>/dev/null)
        echo "$name=$val" >> sysfs-gpu-all.txt
    fi
done
cat sysfs-gpu-all.txt | grep -i "psp\|nbio\|fw\|firmware\|vbios\|ip\|discovery" > sysfs-gpu-key.txt 2>&1
cat sysfs-gpu-key.txt | tee -a _summary.txt

# === 9. Try register read via devmem2 if available ===
echo "=== 9. Direct MMIO reads (BAR5) ===" | tee -a _summary.txt
if command -v devmem2 &>/dev/null; then
    echo "devmem2 found, reading BAR5 registers..."
    BAR5=0xFE800000
    # Read PSP C2PMSG_81 at various MP0 base candidates
    for base in 0x00000 0x04000 0x08000 0x10000 0x14000 0x18000 0x1C000; do
        addr=$((BAR5 + base*4 + 0x244))
        val=$(devmem2 $addr 2>/dev/null | grep "Value at address" | awk '{print $NF}')
        printf "BAR5+0x%05X (MP0_base=0x%05X) C2PMSG_81 = %s\n" $((base*4 + 0x244)) $base "$val" >> devmem-psp.txt
    done
    # Read GPU_ID
    val=$(devmem2 0xFE800000 2>/dev/null | grep "Value at address" | awk '{print $NF}')
    echo "GPU_ID at BAR5+0x0000 = $val" >> devmem-psp.txt
    cat devmem-psp.txt | tee -a _summary.txt
else
    echo "devmem2 not available (try: sudo apt install devmem2 or use busybox devmem)" | tee -a _summary.txt
fi

# === 10. Try to read PCI config space directly ===
echo "=== 10. PCI config space ===" | tee -a _summary.txt
if command -v setpci &>/dev/null; then
    echo "Reading PCI config for 01:00.0:" | tee -a _summary.txt
    for reg in 0x00 0x04 0x08 0x0C 0x10 0x14 0x18 0x1C 0x20 0x24 0x28 0x2C 0x30 0x34 0x38 0x3C 0x40 0x44 0x48 0x4C 0x50; do
        val=$(setpci -s 01:00.0 ${reg}.L 2>/dev/null)
        echo "  01:00.0 +0x${reg} = 0x${val}" >> pci-config-f0.txt
    done
    cat pci-config-f0.txt | tee -a _summary.txt
    # Read PSP function config
    echo "Reading PCI config for 01:00.2 (PSP):" | tee -a _summary.txt
    setpci -s 01:00.2 0x00.L 2>/dev/null || echo "setpci failed for function 2"
else
    echo "setpci not available"
fi

# === 11. Check if amdgpu debugfs has psp ring info ===
echo "=== 11. Additional info ===" | tee -a _summary.txt
ls -la /sys/kernel/debug/dri/ > debugfs-dri.txt 2>&1
cat debugfs-dri.txt | tee -a _summary.txt

# Check IP discovery via amdgpu if available
cat /sys/class/drm/card1/device/ip_discovery 2>/dev/null > ip_discovery.txt 2>&1 && echo "IP discovery available" || echo "No IP discovery"
cat /sys/class/drm/card1/device/ip_block 2>/dev/null > ip_block.txt 2>&1 && echo "IP block available" || echo "No IP block"

# === Summary ===
echo "" | tee -a _summary.txt
echo "=== COLLECTION COMPLETE ===" | tee -a _summary.txt
echo "Output: $(pwd)" | tee -a _summary.txt
echo "Copy to Windows: scp -r bc250-bios-info/ user@windows-ip:C:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/third-party/linuxinfo/" | tee -a _summary.txt

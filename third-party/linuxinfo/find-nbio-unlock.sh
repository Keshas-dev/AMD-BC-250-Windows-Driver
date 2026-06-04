#!/bin/bash
# CachyOS NBIO unlock patch finder
# paleisti: sudo bash find-nbio-unlock.sh
# Rezultatai išeina i /tmp/nbio_find/

OUTDIR="/tmp/nbio_find"
rm -rf "$OUTDIR" 2>/dev/null; mkdir -p "$OUTDIR"
cd "$OUTDIR"

echo "=== CachyOS NBIO Unlock Finder ===" | tee _summary.txt
echo "Date: $(date)" | tee -a _summary.txt
uname -a | tee -a _summary.txt

# 1. Ieskome NBIO unlock kodo amdgpu kode
echo "" | tee -a _summary.txt
echo "=== 1. Ieskome 0xB8/offset b8 amdgpu kode ===" | tee -a _summary.txt
for src in /usr/src/*/drivers/gpu/drm/amd/; do
    if [ -d "$src" ]; then
        grep -rn "0xB8\|0xb8\|0x0B8\|offset.*b8\|config.*b8" "$src" --include="*.c" --include="*.h" 2>/dev/null | grep -iv "BIT\|b800\|b80[0-9a-f]\|0xb80\|0xB80\|81b8\|82b8\|8b8\|fb8\|dead\|beef\|0x1b8\|0x2b8\|0x3b8\|0x4b8\|0x5b8\|0x6b8\|0x7b8\|0x9b8\|0xab8\|0xbb8\|0xcb8\|0xdb8\|0xeb8\|bar_offset\|reg_offset\)" > amdgpu_b8_search.txt 2>&1
        wc -l amdgpu_b8_search.txt | tee -a _summary.txt
        cat amdgpu_b8_search.txt | head -30 | tee -a _summary.txt
    fi
done

# 2. Ieskome "Unexpected write" ar "kernel-exclusive" kode
echo "" | tee -a _summary.txt
echo "=== 2. Ieskome 'Unexpected write' / 'kernel-exclusive' ===" | tee -a _summary.txt
grep -rn "Unexpected write\|kernel-exclusive\|cyan-skillfish" /usr/src/*/ 2>/dev/null | head -20 > unexpected_write.txt
cat unexpected_write.txt | tee -a _summary.txt

# 3. Ieskome bc250-specific NBIO kodo
echo "" | tee -a _summary.txt
echo "=== 3. Ieskome cyan_skillfish NBIO specifiniu ==" | tee -a _summary.txt
grep -rn "CYAN_SKILLFISH\|CHIP_CYAN_SKILLFISH\|cyan_skillfish" /usr/src/*/drivers/gpu/drm/amd/amdgpu/nbio* 2>/dev/null | tee -a _summary.txt
grep -rn "CYAN_SKILLFISH\|CHIP_CYAN_SKILLFISH\|cyan_skillfish" /usr/src/*/drivers/pci/ 2>/dev/null | tee -a _summary.txt

# 4. Ieskome "pci_write_config" NBIO kodo
echo "" | tee -a _summary.txt
echo "=== 4. Ieskome pci_write_config NBIO unlock ===" | tee -a _summary.txt
grep -rn "pci_write_config_dword\|pci_write_config_byte\|pci_write_config_word" /usr/src/*/drivers/gpu/drm/amd/amdgpu/ --include="*.c" 2>/dev/null | head -30 | tee -a _summary.txt

# 5. Tikrinam ar yra bc250 kernel patcha
echo "" | tee -a _summary.txt
echo "=== 5. Ieskome bc250 patch failo ===" | tee -a _summary.txt
find /usr/src -name "*bc250*" -o -name "*skillfish*" 2>/dev/null | head -10 | tee -a _summary.txt
find /usr/src -name "*.patch" 2>/dev/null | head -20 | tee -a _summary.txt

# 6. Tikrines kernelio configa
echo "" | tee -a _summary.txt
echo "=== 6. Kernel config NBIO/BC250 nustatymai ===" | tee -a _summary.txt
zgrep -i "bc250\|skillfish\|NBIO\|PP_DEBUG\|DEBUG_WRITE" /proc/config.gz 2>/dev/null | head -20 | tee -a _summary.txt

# 7. Skaityti NBIO registrus per debugfs
echo "" | tee -a _summary.txt
echo "=== 7. Skaityti NBIO/PSP registrus ===" | tee -a _summary.txt
# Rasti /sys/kernel/debug/dri/ path
for card in /sys/kernel/debug/dri/*; do
    if [ -d "$card" ] && [ -f "$card/amdgpu_regs" ]; then
        echo "Registers at $card:" | tee -a _summary.txt
        # Skaityti NBIO C100/C180
        cat "$card/amdgpu_smn" 2>/dev/null | head -50 | tee -a "$(basename $card)_smn.txt"
        cat "$card/amdgpu_nbio" 2>/dev/null | head -50 | tee -a "$(basename $card)_nbio.txt"
    fi
done

# 8. Skaityti MMIO per devmem2
echo "" | tee -a _summary.txt
echo "=== 8. Skaityti PSP/NBIO registrus per devmem ===" | tee -a _summary.txt
if command -v devmem2 &>/dev/null; then
    BAR5=0xFE800000
    BAR5_END=$((BAR5 + 0x80000))
    echo "BAR5: $BAR5 - $BAR5_END (512KB)"
    for off in 0xC100 0xC180 0xC104 0xC184 0x50D0 0x2004; do
        addr=$((BAR5 + off))
        val=$(devmem2 $addr 2>/dev/null | grep "Value at address" | awk '{print $NF}')
        printf "BAR5+0x%04X = %s\n" $off "$val" | tee -a _summary.txt
    done
    # Skaityti root complex PCI config 0xB8 per setpci
    if command -v setpci &>/dev/null; then
        val=$(setpci -s 00:00.0 0xB8.L 2>/dev/null)
        echo "00:00.0[0xB8] = 0x$val" | tee -a _summary.txt
    fi
else
    echo "devmem2 nerastas" | tee -a _summary.txt
    # Bandyti per busybox devmem
    if command -v devmem &>/dev/null; then
        for off in 0xC100 0xC180 0x50D0 0x2004; do
            addr=$((0xFE800000 + off))
            val=$(devmem $addr 2>/dev/null)
            printf "BAR5+0x%04X = 0x%08X\n" $off $val | tee -a _summary.txt
        done
    fi
fi

# 9. Paskaityti amdgpu ringus
echo "" | tee -a _summary.txt
echo "=== 9. GPU ringu info ===" | tee -a _summary.txt
cat /sys/kernel/debug/dri/*/amdgpu_ring_gfx 2>/dev/null | head -20 | tee -a _summary.txt
cat /sys/kernel/debug/dri/*/amdgpu_ring_sdma 2>/dev/null | head -20 | tee -a _summary.txt

# 10. ar yra rocm/kfd info
echo "" | tee -a _summary.txt
echo "=== 10. KFD/ROcm info ===" | tee -a _summary.txt
cat /sys/class/kfd/kfd/topology/nodes/*/properties 2>/dev/null | head -30 | tee -a _summary.txt

# 11. Pacman pacheckinti kokie kernelio pachetai instaliuoti
echo "" | tee -a _summary.txt
echo "=== 11. Instaliuoti kernelio pachetai ===" | tee -a _summary.txt
pacman -Q | grep -i "linux\|kernel\|cachy" 2>/dev/null | tee -a _summary.txt

# 12. Pilnas dmesg apie amdgpu
echo "" | tee -a _summary.txt
echo "=== 12. Pilnas dmesg logas ===" | tee -a _summary.txt
dmesg | grep -i "amdgpu\|psp\|nbio\|smu\|cyan\|skillfish" > dmesg-amdgpu-filtered.txt 2>&1
wc -l dmesg-amdgpu-filtered.txt | tee -a _summary.txt

echo "" | tee -a _summary.txt
echo "=== BAIGTA ===" | tee -a _summary.txt
echo "Rezultatai: $OUTDIR"

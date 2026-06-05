# PSP Injection via EFI Shell - BC-250

## Quick Start (3 min)

### Prerequisites
- USB drive (FAT32 formatted)
- BC-250 system with BIOS access

### Step 1: Copy files to USB
```
USB/
├── test_psp.nsh        ← Register test script
├── inject_psp.nsh      ← PSP injection script
└── full_inject.nsh     ← Full injection sequence
```

### Step 2: Boot from USB
1. Insert USB into BC-250
2. Enter BIOS (DEL or F2 during boot)
3. Set USB as first boot device
4. Save and reboot

### Step 3: Run in EFI Shell
```shell
Shell> fs0:
fs0:\> test_psp.nsh
```

### Step 4: Analyze results

**If C2PMSG_35/36 show real values (NOT 0xFFFFFFFF):**
```
NBIO is OPEN at EFI boot time!
Run: inject_psp.nsh
Then boot Windows - driver should work!
```

**If C2PMSG_35/36 show 0xFFFFFFFF:**
```
NBIO is already locked.
Disable NBIO DEV in BIOS:
  Advanced > AMD CBS > NBIO > Device Exclusion Vector > Disabled
Then try again.
```

## Register Map

| Register | BAR5 Offset | Physical Address | Description |
|----------|-------------|------------------|-------------|
| C2PMSG_35 | 0x5818C | 0xFE85818C | Bootloader command |
| C2PMSG_36 | 0x58190 | 0xFE858190 | Firmware address |
| C2PMSG_64 | 0x58200 | 0xFE858200 | TOS mailbox |
| C2PMSG_81 | 0x58244 | 0xFE858244 | Sign of Life |
| GRBM_STATUS | 0x0004 | 0xFE800004 | GPU status |

## Firmware Address

The firmware address in C2PMSG_36 must be:
- Physical address >> 20 (MB boundary)
- Example: If firmware is at 0x7E400000, write 0x000007E4

## Commands Reference

### Read register (mm with 0)
```
mm 0xFE858244 0x00000000 -w 4
```

### Write register
```
mm 0xFE858190 0x000007E4 -w 4
```

### Wait (stall in microseconds)
```
stall 1000000    # 1 second
```

## Full Injection Sequence

```shell
# 1. Test registers
test_psp.nsh

# 2. If registers accessible, inject
inject_psp.nsh

# 3. Check SOL - if bit31 set, SOS is alive
# 4. Boot Windows
```

## Troubleshooting

### "mm" command not found
Your EFI Shell doesn't have the mm command. Use a different EFI Shell or compile one with EDK2.

### C2PMSG_35/36 return 0xFFFFFFFF
NBIO is locked at boot time. Try:
1. Disable NBIO DEV in BIOS
2. Disable IOMMU in BIOS
3. Set PSP Support = Enabled in BIOS

### SOS not starting after injection
Check firmware address:
1. Verify firmware is loaded to correct physical address
2. Try addr >> 12 instead of addr >> 20
3. Check if firmware file is correct (262,656 bytes)

## Next Steps

After successful injection:
1. Boot Windows
2. Run `test-gpu-ioctls.exe` - should show more PASS results
3. GRBM_STATUS should return real values (not 0xFFFFFFFF)
4. GFX/CP/SDMA registers should be accessible

## Alternative: RWEverything EFI

If mm command is not available:
1. Download RWEverything EFI version (RwEfi.efi)
2. Copy to USB
3. Run in EFI Shell: `RwEfi.efi`
4. Use GUI to read/write MMIO registers

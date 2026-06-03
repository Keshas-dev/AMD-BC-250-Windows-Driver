# UEFI Tools for BC-250 BIOS Analysis

## 1. UEFITool NE A74 (Windows 64-bit)
Download: https://github.com/LongSoft/UEFITool/releases/download/A74/UEFITool_NE_A74_win64.zip

## 2. UEFIExtract (same release)
Download: https://github.com/LongSoft/UEFITool/releases/download/A74/UEFIExtract_NE_A74_win64.zip

## 3. IFRExtractor
Download: https://github.com/LongSoft/IFRExtractor/releases
(Or search GitHub for "IFRExtractor releases")

## 4. setup_var.efi
Search GitHub for "setup_var.efi" or "setup_var_3.efi"
Common sources:
- https://github.com/datasone/GRUB-Modded-SetupVar
- https://github.com/ocasion/setup_var.efi

## 5. Modified GRUB Shell (for setup_var)
Search for "GRUB modded shell with setup_var"

---

## Usage Steps:

### Step 1: Analyze BIOS with UEFITool
1. Open UEFITool
2. Load BIOS file (BC250_3.00_CHIPSETMENU.ROM)
3. Ctrl+F -> Text -> Search for "IOMMU", "PSP", "NBIO"
4. Extract the Setup module as setup.bin

### Step 2: Convert with IFRExtractor
1. Drag setup.bin onto ifrextractor.exe
2. Open setup.txt
3. Search for:
   - IOMMU -> find VarOffset
   - PSP Support -> find VarOffset
   - NBIO -> find VarOffset
   - DMA Protection (DEV) -> find VarOffset

### Step 3: Create UEFI Shell USB
1. Format USB as FAT32
2. Create EFI\BOOT\ directory
3. Copy setup_var.efi as EFI\BOOT\BOOTX64.EFI
4. Boot from USB -> UEFI Shell

### Step 4: Modify NVRAM (UEFI Shell)
```
setup_var_3 0xXXXX 0x00  (IOMMU disable)
setup_var_3 0xYYYY 0x00  (PSP disable)
setup_var_3 0xZZZZ 0x00  (NBIO DEV disable)
```

### Step 5: Boot Windows and Test
1. Reboot into Windows
2. Check if BC-250 status changed
3. Run PSP test
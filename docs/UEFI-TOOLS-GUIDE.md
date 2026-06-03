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

## Finding VarOffset from UEFITool search results

After finding "IOMMU", "psp", "NBO" in UEFITool, the VarOffset is in the binary data nearby.

### Method 1: Look for IFR (Internal Form Representation) patterns
In the Information panel or hex view, look for patterns like:
```
VarStoreInfo (VarOffset/Offset): 0xNNN
```

### Method 2: Search for "VarOffset" text in the same modules
1. Double-click on one of the IOMMU search results in UEFITool tree
2. Right-click -> "Extract body" or "Extract as is"
3. Open with hex editor (HxD, etc.)
4. Search backwards from the "IOMMU" string for patterns like:
   - `2B 00` (Form ID tag)
   - `01 00` (VarStore reference)

### Method 3: Extract the Setup module and use IFRExtractor
If UEFITool shows a module named "Setup" or "SetupUtility":
1. Right-click -> Extract body
2. Save as setup.bin
3. Download IFRExtractor from: https://github.com/LongSoft/IFRExtractor/releases
4. Run: ifrextractor.exe setup.bin
5. Open setup.txt and search for IOMMU, PSP

### Common VarOffset patterns (AMI BIOS):
| Setting | Typical Offset | Disable Value |
|---------|---------------|---------------|
| IOMMU / AMD-Vi | 0x1A2 - 0x1A5 | 0x00 |
| PSP Support | 0x4F0 - 0x4F5 | 0x00 |
| fTPM | 0x4F1 - 0x4F6 | 0x00 |
| NBIO Options | 0x500+ range | varies |

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
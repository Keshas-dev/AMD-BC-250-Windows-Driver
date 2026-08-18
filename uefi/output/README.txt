# BC-250 GPU WGP Unlock — USB Boot Instrukcijos

## Reikia:
- USB doris (FAT32 formatuotas)
- BC-250 su Windows
- UEFI Shell (standartinis arba pakoreguotas)

## USB Paruošimas:

1. **Formatuoji USB į FAT32**
   - Windows: dešinis klikas → Format → FAT32
   - Linux: mkfs.fat -F 32 /dev/sdX1

2. **Nukopijuoji šiuos failus į USB šaknį:**
   ```
   USB:\
   ├── wgp_unlock_log.nsh   (pagrindinis skriptas su log)
   ├── test_access.nsh     (paprastas testas)
   ├── wgp_unlock.nsh      (papras unlock skriptas)
   └── README.txt          (šis failas)
   ```

## Boot Instrukcijos:

### 1. Boot iš USB
- Įjunk BC-250
- Spausk DEL arba F2 → BIOS
- Boot → Boot Option #1 → pasirink tavo USB
- Save & Exit
- ARBA: Spausk F11/F12 → Boot Menu → pasirink USB

### 2. UEFI Shell
Po USB boot turėtų užkrauti UEFI Shell:
```
Shell> fs0:
fs0:\> wgp_unlock_log.nsh
```

### 3. Rezultatas
Skriptas:
- Testuos visus GPU registrus
- Bands rašyti SPI_PG = 0x1F
- Įrašys viską į `wgp_log.txt`
- Parodys rezultatus ekrane

### 4. Po testo
- Nukopijuoji `wgp_log.txt` iš USB į Windows
- Praneši man rezultatus

## Jei Nėra UEFI Shell:

Kai kuriuose BIOS nėra UEFI Shell. Tada:
1. Atsisiųsi UEFI Shell iš: https://github.com/tianocore/edk2/releases
2. Nukopijuoj `Shell_Full.efi` į USB kaip `bootx64.efi`
3. Bootinį iš USB

## Jei NBIO Locked (visur 0xFFFFFFFF):

BIOS → Advanced → AMD CBS → NBIO Common Options → Device Exclusion Vector → Disabled

Arba bandyk kitą BIOS versiją.

## Log Analizė:

Po testo siųsk `wgp_log.txt`. Mes žinosime:
- Ar adresas prieinamas
- Ar rašymas veikia
- Ar reikia kitokio offset
- Koks tikslus SPI_PG adresas

---

Pastabos:
- `mm` komanda = Memory Modify (skirta MMIO access)
- `-mm` = MMIO nei RAM
- `-b 4` = 4 baitai (32-bit registras)
- 0xFFFFFFFF = adresas neprieinas (NBIO lock)

---

Sėkmės! 🚀

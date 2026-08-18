# BC-250 3D Enablement — Bendra Analizė ir PSP Bridge Planas

**Data:** 2026-08-07  
**Autorius:** AI Assistant  
**Statusas:** Research / Planavimas

---

## 1. Santrauka

BC-250 yra PS5 Oberon APU (CPU+GPU+RAM viename pakete) su 40 CU (Compute Units) iš kurių tik 24 aktyvios. Papildomos 16 CU yra **SOS-locked** (ne išdegusios) — tai firmware policy, ne hardware defect.

**Pagrindinis blokuoja:** `SPI_PG_ENABLE_STATIC_WGP_MASK` (BAR5 0x5C3C) register yra užrakintas PSP Secure OS ir negali būti perrašytas per host BAR5 MMIO Windows WDM kontekste.

**Rastas sprendimo kelias:** PSP (Platform Security Processor) per PCIe BAR2 (0xFE700000) gali turėti priėjimą prie GPU registru nepriklausomai nuo host SOS lock.

---

## 2. Hardware Architektūra

### 2.1 PCIe Devices

| Function | Device | VEN:DEV | BAR | Physical | Paskirtis |
|---|---|---|---|---|---|
| 0 | GPU (GFX) | 1022:13FE | BAR5 | 0xFE800000 | GC/DF/MMHUB registrai (512KB) |
| 2 | PSP | 1022:143E | BAR2 | 0xFE700000 | PSP kontrolė + GPU access window? |

### 2.2 APU Die Struktura

```
AMD BC-250 (Cyan Skillfish / GFX1013)
├── CPU (8× Zen2 cores, 6 unlocked)
├── GPU (RDNA2, 40 CU, 24 unlocked)
│   ├── GC (Graphics Core) — BAR5 0xFE800000
│   ├── DF (Data Fabric)
│   ├── MMHUB
│   └── 40 CU (SE0/SH0: 10 CU, SE0/SH1: 10 CU, SE1/SH0: 10 CU, SE1/SH1: 10 CU)
├── PSP (Platform Security Processor)
│   ├── BAR0: 0xFD600000 (PSP kontrolė)
│   └── BAR2: 0xFE700000 (GPU access window?)
├── SMU (System Management Unit)
│   └── Firmware: Smu.bin (v88.6.0)
└── SOS (Secure OS)
    └── Firmware: Sos.bin (controls SPI_PG lock)
```

---

## 3. Kas Veikia (Patikrinta)

| Funkcija | Statusas | Pastaba |
|---|---|---|
| GPU driver (atikmdag.sys) | ✅ | WDM IOCTL, BAR5 mapping, SMU access |
| PSP firmware load | ✅ | SYSDRV/SOS/SMC per C2PMSG |
| SOS alive | ✅ | C2PMSG_81=0xF0000010 |
| SMU | ✅ | v88.6.0 @ 1500MHz, 931mV |
| CPU core unlock (6→8) | ✅ | SMU 0x98 per Q3 (rw-r-r-0644/bc250-core-unlock) |
| Register read/write | ✅ | 80+ registru skaitomi/rašomi |
| SMU frequency control | ✅ | Q0/Q3 mailbox messages |

## 4. Kas Neveikia (Pagrindinė Blokada)

| Funkcija | Statusas | Priežastis |
|---|---|---|
| **SPI_PG write** | ❌ | SOS-locked register |
| **WGP unlock** | ❌ | SPI_PG = 0x00000000 visada |
| **40 CU enable** | ❌ | Reikia SPI_PG = 0x1F |

### 4.1 SPI_PG Write Bandymi (Visi NEVEIKIA)

| Metas | Metodas | Rezultatas |
|---|---|---|
| Driver init | Direct BAR5 write | SPI_PG = 0 (locked) |
| Driver init | Per-bank (Linux layout) | SPI_PG = 0 (locked) |
| Driver init | Per-bank (alt layout) | SPI_PG = 0 (locked) |
| IOCTL | Direct BAR5 write | SPI_PG = 0 (locked) |
| IOCTL | Per-bank GRBM_GFX_INDEX | SPI_PG = 0 (locked) |
| SMU | Q0 msg 0x18 RequestActiveWgp | Timeout (-100) |
| SMU | Q3 msg 0x98 (ungated SMN write) | Neina SPI_PG |
| Continuous write | 100× repeated | SPI_PG = 0 (locked) |
| Different values | 0x01-0xFFFFFFFF | SPI_PG = 0 (locked) |
| Interrupts off | KeRaiseIrql(HIGH_LEVEL) | SPI_PG = 0 (locked) |

### 4.2 Kodėl Linux Veikia Bet Windows Ne

| Aspektas | Linux | Windows |
|---|---|---|
| Driver load timing | Initramfs (labai anksti) | Po boot (vėliau) |
| SPI_PG write timing | Prieš SOS lock | Po SOS lock |
| Access method | debugfs (UMR) | WDM IOCTL |
| Privilege | Kernel + debugfs | Kernel (Ring 0) |
| Result | ✅ SPI_PG = 0x1F sticks | ❌ SPI_PG = 0 locked |

---

## 5. PSP Bridge — Sprendimo Koncepcija

### 5.1 Idėja

PSP per savo PCIe BAR2 (0xFE700000) gali turėti priėjimą prie GPU registru per DF (Data Fabric) arba NBIO (North Bridge I/O). Šis priėjimas **nėra SOS-locked** nes PSP rašo ne per host MMIO, o per PSP PCIe function.

### 5.2 Teorinis Veikimas

```
Host (Windows WDM)
    │
    ├──→ GPU BAR5 (0xFE800000) → SPI_PG = SOS-locked ❌
    │
    └──→ PSP driver (PspDriver.sys)
            │
            └──→ PSP BAR2 (0xFE700000) → DF fabric → GC SPI_PG ✅?
```

### 3.3 Reikia Atlikti

| # | Užduotis | Būsena |
|---|---|---|
| 1 | Įdiegti PSP driver | ⏳ Reikia |
| 2 | Map'inti PSP BAR2 (0xFE700000) | ⏳ Reikia |
| 3 | Patikrinti ar BAR2 suteikia priėjimą prie GC | ⏳ Reikia |
| 4 | Jei taip — rašyti SPI_PG = 0x1F per PSP | ⏳ Reikia |
| 5 | Verifikuoti QueryActiveWgp = 4 | ⏳ Reikia |

---

## 6. PSP Bridge — Techninis Planas

### 6.1 PSP Driver Setup

```c
// PSP PCIe configuration
#define PSP_PCI_DEVICE  L"PCI\\VEN_1022&DEV_143E"
#define PSP_BAR2_PHYSICAL  0xFE700000ULL
#define PSP_BAR2_SIZE      0x100000  // 1MB (guess)

// Map PSP BAR2
PHYSICAL_ADDRESS pspBar2Pa;
pspBar2Pa.QuadPart = PSP_BAR2_PHYSICAL;
PVOID pspBar2Va = MmMapIoSpace(pspBar2Pa, PSP_BAR2_SIZE, MmNonCached);

// Hypothetical: PSP BAR2 offset to GC register window
// This needs to be discovered by probing
#define PSP_GC_WINDOW_OFFSET  0x10000  // Unknown — needs analysis
```

### 6.2 SPI_PG Write per PSP

```c
NTSTATUS WriteSpiPgViaPsp(PVOID PspBar2Va) {
    // Try direct SPI_PG address through PSP window
    ULONG spiBefore = READ_REGISTER_ULONG(
        (PULONG)((PUCHAR)PspBar2Va + PSP_GC_WINDOW_OFFSET + 0x5C3C));
    
    // Write unlock value
    WRITE_REGISTER_ULONG(
        (PULONG)((PUCHAR)PspBar2Va + PSP_GC_WINDOW_OFFSET + 0x5C3C), 0x1F);
    
    // Also write RLC_PG
    WRITE_REGISTER_ULONG(
        (PULONG)((PUCHAR)PspBar2Va + PSP_GC_WINDOW_OFFSET + 0x3D64), 0x1F);
    
    // Verify
    ULONG spiAfter = READ_REGISTER_ULONG(
        (PULONG)((PUCHAR)PspBar2Va + PSP_GC_WINDOW_OFFSET + 0x5C3C));
    
    return (spiAfter == 0x1F) ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
}
```

### 6.3 Probe Planas

Kad nerastume PSP_GC_WINDOW_OFFSET, reikia:

1. **Perskaityti PSP BAR2** — kokie registrai matomi
2. **Iešoti SPI_PG "signature"** — gal PSP BAR2 turi mirror arba window
3. **Bandyti skirtingus offset'us** — 0x0, 0x10000, 0x20000, etc.
4. **Patikrinti ar rašymas "stuck"** — jei offset teisingas, SPI_PG turėtų persirašyti

---

## 7. Alternatyvūs Keliai (Jei PSP Bridge Neveiks)

### 7.1 SMC Firmware Modifikacija

- **Idėja:** Modifikuoti Smu.bin kad SMU pats parašytų SPI_PG = 0x1F boot metu
- **Problema:** SMC firmware formatas nežinomas, nėra hardcoded SPI_PG adresų
- **Statusas:** Neprobauta

### 7.2 UEFI Driver

- **Idėja:** Sukurti UEFI DXE driver kuris rašo SPI_PG prieš OS boot
- **Problema:** Reikia UEFI development setup
- **Statusas:** RescueMei jau padarė tai CPU core unlock (bc250-core-unlock DXE)

### 7.3 VBIOS Mod

- **Idėja:** Modifikuoti VBIOS kad parašytų SPI_PG per POST
- **Problema:** VBIOS signed, modifikacija sunku
- **Statusas:** Neprobauta

### 7.4 Linux Pasibandymas

- **Idėja:** Naudoti Linux su `bc250_cc_write_mode=3` kernel parameter
- **Statusas:** Žinoma kad veikia (1.61x compute scaling)
- **Neigiama:** Reikia Linux, ne Windows

---

## 8. Rekomendacijos

### 8.1 Trumpalaikės (1-2 dienos)

1. **Dokumentuoti** visus Windows WDM bandymus (šis dokumentas)
2. **Įdiegti PSP driver** ir patikrinti PSP BAR2 access
3. **Probe PSP BAR2** — ar suteikia priėjimą prie GC

### 8.2 Vidutalaikės (3-7 dienos)

1. **Analizuoti PSP firmware** (Sos.bin) — gal turi GC init table
2. **Bandyti SMC firmware modifikaciją** — jei formatas žinomas
3. **Konsultuotis su Linux community** — gal žino PSP→GC mapping

### 8.3 Ilgalaikės (1-4 savaitės)

1. **Sukurti UEFI driver** (kaip RescueMei CPU unlock)
2. **VBIOS modifikacija** — jei kiti keliasi neveiks
3. **Linux kernel integration** — kaip fallback

---

## 9. Svarbos Faktai

| Fakas | Reikšmė |
|---|---|
| BC-250 turi 40 CU | Tik 24 aktyvios, 16 harvested |
| SPI_PG = 0x1F reikalingas | Kiekvienam WGP (5 WGP = 40 CU) |
| SPI_PG yra SOS-locked | Host BAR5 writes neveikia |
| Linux gali rašyti SPI_PG | Per debugfs/UMR init metu |
| PSP turi BAR2 = 0xFE700000 | Gali suteikti priėjimą prie GC |
| PSP driver jau egzistuoja | Bet nenaudojamas (deprecated) |
| SMU 0x98 veikia | Bet rašo 0xFF į SMN, ne į GC |

---

## 10. Išvados

1. **Windows WDM tiesiogiai negali atrakinti WGP** — SPI_PG yra SOS-locked
2. **PSP Bridge yra perspektyviausias kelias** — PSP gali rašyti į GC per savo PCIe
3. **Reikia PSP driver io diegimo ir testavimo** — ar BAR2 suteikia priėjimą
4. **SMC mod ir UEFI driver yra atsarginiai kelai** — jei PSP neveiks
5. **Linux yra žinomas veikiantis kelias** — galima naudoti kaip fallback

---

## 11. Nuorodos

- [duggasco/bc250-40cu-unlock](https://github.com/duggasco/bc250-40cu-unlock) — Linux 40CU unlock
- [rw-r-r-0644/bc250-core-unlock](https://github.com/rw-r-r-0644/bc250-core-unlock) — CPU core unlock
- [cyan-skillfish-governor](https://github.com/cyan-skillfish-governor) — SMU management
- [bc250_smu_oc](https://github.com/bc250_smu_oc) — SMU overclocking
- [RescueMei/BC250-DXE-SMU-Core-Unlock](https://github.com/RescueMei/BC250-DXE-SMU-Core-Unlock) — UEFI driver

---

## 12. Pakeitimų Istorija

| Data | Pokytis |
|---|---|
| 2026-08-07 | Pradinis dokumentas suvidalintas |
| 2026-08-07 | Integruotas PSP firmware load į GPU driver |
| 2026-08-07 | Ištaisytas CP_HQD offset bug (0x9148→0x9138) |
| 2026-08-07 | Pritaikytas Linux gfx10 GRBM_GFX_INDEX layout |

---

*Document generated by AI Assistant — 2026-08-07*

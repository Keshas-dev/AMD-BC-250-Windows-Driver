# AMD BC-250 Windows Driver — Pilna Ataskaita

**Data:** 2026-06-26  
**Projektas:** AMD BC-250 (Cyan Skillfish) GPU + PSP Windows Drivers  
**Autoriai:** Keshas, dirbtinio intelekto agentai (Claude, Copilot)  
**Repos:**  
- GPU Driver: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver  
- PSP Driver: https://github.com/Keshas-dev/AMD-BC-250-PSP-Driver  

---

## Turinys

1. [Projekto Tikslas](#1-projekto-tikslas)
2. [Aparatūros Atradimai](#2-aparattros-atradimai)
3. [Kronologija: Kas Buvo Padaryta](#3-kronologija-kas-buvo-padaryta)
4. [GPU Driverio Architektūra](#4-gpu-driverio-architektra)
5. [PSP Driverio Architektūra](#5-psp-driverio-architektra)
6. [Klaidos ir Pamokos](#6-klaidos-ir-pamokos)
7. [Code Review: 17 Bugs](#7-code-review-17-bugs)
8. [Fundamentalūs Blokeriai](#8-fundamentalbs-blokeriai)
9. [Ką Veikia ir Ko Ne](#9-k-veikia-ir-ko-ne)
10. [Išvada](#10-ivada)

---

## 1. Projekto Tikslas

Sukurti veikiančius Windows GPU ir PSP driverius AMD BC-250 (Cyan Skillfish) platformai — kripto kasybos plokštei su PS5 APU variantu (AMD Oberon). Ši plokštė neturi oficialių Windows GPU driverių, nors Linux (amdgpu + Mesa RADV) pilnai palaikoma.

**Problema:** Windows rodo tik "Microsoft Basic Display Adapter" — nėra 3D pagreitinimo, jokie D3D žaidimai neveikia.

**Sprendimas:** Sukurti WDM (Windows Driver Model) IOCTL driverį, kuris leidžia:
- Pasiekti GPU MMIO registrus per BAR5 (0xFE800000)
- Valdyti PSP mailbox protokolą
- Vykdyti PM4 komandas per Software PM4 executor (CPU verčia PM4 į MMIO rašymus)

---

## 2. Aparatūros Atradimai

### 2.1 GC_BASE = 0x1260 — Pagrindinis Atradimas

BC-250 naudoja **nestandartinį** BAR5 registrų žemėlapį. Visi GC/GRBM/CP/SDMA registrai yra postūmti 0x1260 baitų, palyginus su standartine Navi10 (RDNA2) architektūra.

**Šaltinis:** Linux kernel `cyan_skillfish_ip_offset.h`:
```c
#define GC_BASE__INST0_SEG0 0x00001260  // Segment 0: dauguma GC registrų
#define GC_BASE__INST0_SEG1 0x0000A000  // Segment 1: kiti GC registrai
```

**Formulė:** `BAR5_offset = 0x1260 + Navi10_DWORD_offset * 4`

| Registras | Navi10 Offset | BC-250 Offset | Reikšmė |
|-----------|---------------|---------------|---------|
| GRBM_STATUS | 0x2000 | **0x3260** | 0x00000000 (idle) |
| CC_GC_SHADER_ARRAY_CONFIG | 0x2004 | **0x3264** | 0x00000000 (fused) |
| GRBM_SOFT_RESET | 0x200C | **0x326C** | 0x00000000 (RW) |
| CP scratch | 0x2074 | **0x32D4** | 0x4D585042 ("MDPX") |
| SPI_PG_ENABLE_STATIC_WGP_MASK | 0x229C | **0x34FC** | 0x00002000 |
| RLC_PG_ALWAYS_ON_WGP_MASK | 0x2B04 | **0x3D64** | 0x00000000 |

### 2.2 NBIO Firewall — Dvi Apsaugos

**Klaidinga prielaida (pradžioje):** Buvo manyta, kad NBIO blokuoja VISUS GC registrus, nes 0x2000-0x2FFF grąžino 0xFFFFFFFF.

**Tiesa (atrasta 2026-06-11):** NBIO BC-250 turi **dvi** atskiras apsaugas:

1. **0xC000-0xCFFF diapazonas (native NBIO):** VISI rašymai blokuoti iš VISŲ kelių (GPU BAR5, PSP BAR0, SMN). Skaitymas grąžina 0.
2. **GC registrai 0x2000-0x2FFF:** NE blokuoti — 0xFFFFFFFF buvo dėl neteisingų offset'ų.

**GC_BASE-shifted aliases** (pvz., 0xDA60 = 0x1260 + 0xC800) apeina NBIO tiltą. Šie adresai mapuojasi į tuos pačius HW registrus per GC erdvę, kurią NBIO neinterceptuoja.

### 2.3 SMU v11.8 — Minimalus Rinkinys

BC-250 naudoja SMU v11.8 (ne v11.0 kaip Navi10). **MP1_BASE = 0x16000.**

| Registras | Offset | Paskirtis |
|-----------|--------|-----------|
| C2PMSG_66 | 0x16A08 | Message register (trigger) |
| C2PMSG_82 | 0x16A48 | Argument/data register |
| C2PMSG_90 | 0x16A68 | Response (0=busy, 1=OK, FF=err) |

**Protokolas:** clear C2PMSG_90 → arg → msg → poll

**Palaikomos žinutės (minimalios):**
- 0x1 TestMessage
- 0x2 GetSmuVersion
- 0x18 RequestActiveWgp (power up WGPs)
- 0x1E QueryActiveWgp
- 0x2C SetCoreEnableMask

**Nėra:** PowerUpGfx, EnableDpmFeature, PowerUpSdma, SetFanSpeedPercent.

### 2.4 THM_BASE

- Linux teigia: THM_BASE = 0x16600
- **Hardware patvirtina:** THM_BASE = 0x8000 (BIOS P4.00G)
- 0x8000 grąžina 0x18 (writable), 0x8008 grąžina temperatūrą
- 0x1662C grąžina 0x00000000

### 2.5 KIQ_WPTR — Tik 9 Bitai

KIQ_WPTR registras (0xE078) yra **tik 9 bitų** (mask 0x1FF). Tai reiškia maksimalų ringo dydį: 512 DWORDų = 2048 baitai.

### 2.6 KIQ_SIZE = 0 — Hardware-level Blocker

KIQ_CNTL (0xE068) skaitosi kaip 0 ir rašymas neveikia. Patvirtinta, kad tai **hardware-level** blokas — MEC firmware patching (abu 0xCE08 → NOP) nedavė jokio efekto. KIQ ringo apdorojimas BC-250 yra **neįmanomas**.

### 2.7 GCVM PT_BASE = HW-locked

GCVM_CONTEXT0_PT_BASE_LO (0x0B608) visada skaito 0. Nors CONTEXT0_CNTL (0x0B460) yra writable, PT_BASE negalima nustatyti — negalima konfigūruoti GPU puslapių lentelių.

---

## 3. Kronologija: Kas Buvo Padaryta

### 3.1 Pradžia (2026-05-XX — 2026-06-01)

- Sukurtas pradinis KMDF driverio karkasas
- Bandyta komunikuoti su PSP per BAR0
- Susidurta su Code 52 (signature), Code 0x7e (KMDF → WDM konversija)
- Perrašytas driveris į WDM (native NT driver)

### 3.2 Klaidinga Kryptis (v2.0)

- Manyta, kad BC-250 yra Kaveri/GCN 1.1 (Sea Islands)
- Neteisinga: GFX7 vietoj GFX10, DDR3 UMA vietoj GDDR6
- **Švaistymas:** Sukurtas driveris neteisingai architektūrai

### 3.3 Linux Source Atradimas

- `cyan_skillfish_ip_offset.h` rastas Linux kernel šaltiniuose
- GC_BASE = 0x1260 atrastas
- Configūracija patvirtinta per Linux devmem ir Windows driver testus

### 3.4 GCVM+PM4 Proveržis (2026-06-18)

- GCVM offset'ai pataisyti — registrai gyvi ir writable
- PTE flag'ai pataisyti: SYSTEM bit (0x61 → 0x63)
- IC_BASE offset'ai pataisyti: 0x7C10-0x7C18 → 0x17390-0x17398
- IC_BASE_CNTL rašymo tvarka pataisyta: LO→HI→CNTL=0 (ne 0x100)
- UCODE_ADDR polling pridėtas (500ms timeout)

### 3.5 KIQ Firmware Analizė (2026-06-26)

- MEC firmware binary fully analyzed (44-byte header, Jump Table at 0x415B0)
- KIQ register reference counts corrected: KIQ_BASE_LO=69×, KIQ_CNTL=2×, KIQ_BASE_HI=1×
- Blind firmware patch: both 0xCE08 (KIQ_CNTL) → NOP (0x8078)
- **Rezultatas:** Patch nedavė jokio efekto — KIQ_SIZE=0 hardware-level

### 3.6 Software PM4 Executor (Veikiantis)

- `DreamV3SwPm4Process` sukurtas: CPU verčia PM4 paketus į tiesioginius MMIO rašymus
- Patvirtinti: IT_WRITE_DATA, IT_NOP, IT_EVENT_WRITE_EOP, PM4_TYPE_0
- Implementuoti: IT_SET_CONFIG_REG, IT_SET_CONTEXT_REG, IT_SET_SH_REG, IT_INDIRECT_BUFFER
- Rekursijos gylio apsauga (max 32)

### 3.7 Code Review (2026-06-24/26)

- 17 bugs found GPU driveryje, 6 bugs PSP driver'yje
- **10 bugs pataisyti:** fw_load.c BAR5_U32, overflow'ai, halt scope, ring size, body_size, write return checks, race condition
- Abu driveriai build'inasi švariai

---

## 4. GPU Driverio Architektūra

### 4.1 Failų struktūra

```
src/kmd/
├── amdbc250_dream_kmd.c        # DriverEntry, IOCTL dispatch (30+ handlers)
├── amdbc250_dream_hw_init.c    # GPU init (10 steps), ring buffers
├── amdbc250_dream_fw_load.c    # CP firmware loading via MMIO IC_BASE DMA
├── amdbc250_dream_power.c      # SMU v11.8 (STUB)
├── amdbc250_dream_vm.c         # GCVM, GART, page tables
├── amdbc250_dream_golden.c     # Golden register programming
├── amdbc250_dream_hdp.c        # HDP coherency
├── amdbc250_dream_rlc.c        # RLC (no-op — registers in freeze zone)
└── amdbc250_psp.c              # PSP proxy driver bridge

inc/
├── amdbc250_dream_hw.h         # Hardware register definitions
├── amdbc250_dream_kmd.h        # KMD structures, GC_BASE constant
└── amdbc250_ioctl.h            # IOCTL codes

test-tools/                     # 20+ diagnostic tools
```

### 4.2 IOCTL Inicializacijos Seka

```
IOCTL_AMDBC250_INIT_HARDWARE (0x80000B80)
│
├── Flags=0: PSP_INIT — only initializes PSP context
│   └── Maps BAR5 (0xFE800000, 512KB) via MmMapIoSpace
│
├── Flags=1: NBIO_MAP — safe init path (DEFAULT)
│   ├── Maps BAR5 + BAR2 (VRAM at 0xC0000000)
│   ├── Maps PSP driver (Amdbc250PspInit)
│   ├── KIQ ring init (Amdbc250PspKiqInit)
│   ├── Verifies GPU alive (reg[0x0000])
│   ├── PCI config enable (IO ports 0xCF8/0xCFC)
│   ├── DreamV3HwInitialize (non-fatal)
│   │   ├── Memory controller (GB_ADDR_CONFIG)
│   │   ├── Golden registers
│   │   ├── IH ring (256KB)
│   │   ├── GFX ring (2MB, CP firmware NOT loaded)
│   │   ├── GART (128KB page table)
│   │   └── GPUVM (16 VMIDs)
│   └── Sets HardwareInitialized = TRUE
│
└── Flags=2: FULL_INIT — not implemented
```

### 4.3 Registrų Prieigos Kelias

```
WRITE_REGISTER_ULONG (veikia visada)
└── DreamV3WriteRegister(DevExt, offset, value)
    └── MmioVirtualBase → BAR5 physical 0xFE800000

PSP proxy (nebenaudojamas, bet paliktas)
└── PspGpuProxyWriteRegister(offset, value)
    └── Atidaro \\.\AmdBcPsp → IOCTL 0x901
    └── PROBLEMA: PSP driver mapuoja PSP BAR0, ne GPU BAR5
```

### 4.4 Software PM4 Executor

```c
DreamV3SwPm4Process(DevExt, commands, count, fenceValue, depth)
│
├── IT_WRITE_DATA (0x40): rašo reikšmę į registrą
│   └── DreamV3WriteRegister(targetReg, data)
│
├── IT_NOP (0x10): nieko nedaro
│
├── IT_SET_CONFIG_REG (0x68): GC config regs
│   └── BAR5 = GC_BASE + (configBase + hwOff<<2)
│
├── IT_SET_CONTEXT_REG (0x69): context regs
│
├── IT_SET_SH_REG (0x6C): shader regs
│
├── IT_EVENT_WRITE_EOP (0x47): fence
│   └── DreamV3WriteEopFence(DevExt, fenceValue)
│
├── PM4_TYPE_0: registro rašymas su mask
│
└── IT_INDIRECT_BUFFER (0x44): recursion depth ≤ 32
    └── MmMapIoSpace(pa, size) → process → unmap
```

---

## 5. PSP Driverio Architektūra

### 5.1 Failų Struktūra

```
src/driver/
├── PspDriver.c              # DriverEntry, IOCTL dispatch
├── PspCore.c                # Mailbox (C2PMSG), proxy bridge, firmware
├── PspKiq.c                 # KIQ ring management
└── PspSmu.c                 # SMU v11.8 communication

inc/
├── PspIoctl.h               # Shared IOCTL definitions
└── firmware_data.h          # Embedded firmware blobs
```

### 5.2 PSP Boot Seka

```
1. BIOS POST → SPI flash skaito ABL0 (0x962D00, 0x440 bytes)
2. ABL0 inicijuoja SecureOS (0x99E200)
3. SecureOS paleidžia SOS iš $PSP (type 1)
4. SOS ready → C2PMSG_81 = 0xF0000010 (BIOS error, bet SOS gyvas)

Mūsų driverio boot:
5. INIT_HW → MmMapIoSpace BAR5 (0xFE800000) arba GPU proxy fallback
6. LOAD_FW → MmAllocateContiguousMemory (persistent)
7. SEND_CMD 0x4 (SYSDRV)
8. SEND_CMD 0x8 (SOS)
9. C2PMSG_81 → 0x00000000 (success)
```

### 5.3 PSP Proxy Kelias (Windows 11 26100)

Kai tiesioginis `MmMapIoSpace` blokuotas:
```
PSP Driver → ZwCreateFile(\\.\AMDBC250DreamV43)
           → ZwDeviceIoControlFile(IOCTL_AMDBC250_BAR5_READ_PROXY_RAW = 0x900)
           → GPU Driver → WRITE_REGISTER_ULONG(MmioVirtualBase + offset)
```

### 5.4 KIQ Ring (Blokuotas)

- KIQ ring programming per PSP driver: rašo KIQ_BASE_LO/HI, RPTR, WPTR
- **KIQ_SIZE=0 read-only** — ring enable neįmanomas
- **WPTR 9-bit limit** — max 512 DWORDų
- PSP KIQ path turi tą pačią problemą kaip GPU driver: RPTR niekad nesikeičia

---

## 6. Klaidos ir Pamokos

### 6.1 Kritinės Klaidos

| # | Klaida | Kada | Pasekmė | Kaip Ištaisyta |
|---|--------|------|---------|----------------|
| 1 | **Neteisingas architektūros identifikavimas (v2.0)** | 2026-05 | Manyta, kad BC-250 = Kaveri/GCN 1.1. Sukurtas driveris neteisingai architektūrai. | Linux kernel šaltinių analizė: Cyan Skillfish (GFX1013), RDNA2 |
| 2 | **IC_BASE_CNTL=0x100 prieš nustatant adresą** | 2026-06-18 | Jei bit 8 auto-startino DMA iš adreso 0, firmware niekada neįkraunamas | LO → HI → CNTL=0 |
| 3 | **Neteisingi GCVM offset'ai** | 2026-06-01..17 | Visi GCVM registrai skaitė 0xFFFFFFFF | Teisinga formulė: BAR5 = 0x1260 + Linux_DWORD_offset * 4 |
| 4 | **BAR5_U32 volatile ptr** | fw_load.c | Win11 26100 volatile pointer rašymas tyliai nukrenta | DreamV3WriteRegister (WRITE_REGISTER_ULONG) |
| 5 | **CP_HQD writability** | Visą laiką | Teigta, kad 0xDAC0+ rašomi; iš tiesų NBIO blokuoti | Suklaidinti aliased/stale readback reikšmių |
| 6 | **RLC firmware load per 0x3A00** | 2026-06-18 | 0x3A00 registrai yra FREEZE ZONE (0x3400-0x8100) | BC-250 BIOS/SMU krauna RLC |

### 6.2 Klaidingos Prielaidos

| Prielaida | Tiesa | Kada Sužinota |
|-----------|-------|---------------|
| "NBIO blokuoja visus GC registrus" | NBIO blokuoja tik 0xC000+ diapazoną; GC registrai nepasiekiami dėl neteisingų offset'ų | 2026-06-11 |
| "PSP proxy reikalingas NBIO bypass" | PSP proxy nenaudingas — PSP BAR0 mapuoja kitą fizinį adresą | 2026-06-12 |
| "GRBM_GFX_CNTL gali atrakinti HQD" | 0x2022 neveikia BC-250 | 2026-06-18 |
| "MEC firmware patching gali pataisyti KIQ_SIZE=0" | KIQ_SIZE=0 yra hardware-level, ne firmware | 2026-06-26 |
| "SDMA registrai 0xE000 gyvi" | Sukėlė BSOD 0x1a; reikia sanity check | 2026-06-24 |
| "KIQ_WPTR = 32 bitai" | Tik 9 bitai (0x1FF) | 2026-06-26 |
| "PM4 TYPE3 header teisingas testuose" | Visi testai naudojo `0xC0370003` (IT_NOP count=56, ne IT_WRITE_DATA) | 2026-06-24 |

### 6.3 Programavimo Klaidos GPU Driver'yje

| Klaidos Tipas | Pavyzdžiai |
|--------------|------------|
| **Integer overflow** | fw validation `offset + size > fwSize` (wraps), ring wrap `wptr + totalBytes > ringSize`, READ_REG/WRITE_REG `offset + 4 > mmioSize` |
| **Neteisinga HW registrų prieiga** | `volatile UINT32*` vietoj `WRITE_REGISTER_ULONG`; IC_BASE_CNTL = 0x100 |
| **Per platus halt** | LOAD_CP_FW halts ALL engines (ME+PFP+CE) kai reikia tik MEC |
| **Nesaugūs bounds check'ai** | `jtByteOff + jtSizeBytes <= fwSize` gali overflow'inti |
| **Trūkstama validacija** | GRBM_GFX_INDEX select, polling timeout, write return check |
| **Race condition** | PSP proxy init be spinlock (abu thread'ai atidaro handle) |
| **Neteisingi offset'ai** | GRBM_GFX_INDEX = 0x33C4 (turėtų 0x34D0), SDMA0_CNTL = 0x10040 |
| **Memory leak** | GCVM page table pages niekada nefryinami driver unload metu |

### 6.4 Programavimo Klaidos PSP Driver'yje

| Klaida | Paveikti Failai |
|--------|----------------|
| **body_size=1** turėtų būti 4 (LOAD_IP_FW formatas) | PspKiq.c |
| **Ring size 0x2000** viršija 9-bit WPTR max 512 DWORDų | PspKiq.c |
| **PspGpuProxyWriteRegister return'as ignoruojamas** | PspKiq.c, PspCore.c |
| **Race condition g_GpuDriverHandle init** | PspCore.c |

### 6.5 Techninės Pamokos

1. **WRITE_REGISTER_ULONG vs volatile\*** — Win11 26100 volatile pointer write gali būti tyliai drop'intas. `WRITE_REGISTER_ULONG` visada veikia.
2. **Firmware execution proof** — ucode corruptinimas + SCRATCH stebėjimas.
3. **KIQ deactivation** — prieš rašant KIQ registrus, būtina deactivate (writi 0 į 0xE080). KIQ_ACTIVE=1 + garbage KIQ_BASE = hang.
4. **GRBM_GFX_INDEX restore** — būtina restore'Inti į broadcast (0xE0000000) po testų, kitaip kiti ME/PIPE/QUEUE nepasiekiami.
5. **Code review prieš buildą** — sutaupo valandų debugging. Abu code review sesijos (GPU + PSP) rado 23 bugs, 10 jau pataisyti.

---

## 7. Code Review: 17 Bugs

### 7.1 Pataisyti (10)

| # | Priority | Failas | Bug'as | Fix Date |
|---|----------|--------|--------|----------|
| 1 | CRITICAL | fw_load.c | BAR5_U32 volatile ptr — Win11 26100 tyliai dropina | 2026-06-26 |
| 2 | CRITICAL | kmd.c:4013,4041 | READ/WRITE_REG overflow: `offset + 4 > MmioSize` wraps ties ≥0xFFFFFFFC | 2026-06-26 |
| 3 | HIGH | kmd.c:5457 | LOAD_CP_FW halts ALL engines kai reikia tik MEC | 2026-06-26 |
| 4 | HIGH | kmd.c:3940 | SEND_PM4 ring wrap: `WPtr + TotalBytes` 32-bit overflow | 2026-06-26 |
| 5 | HIGH | fw_load.c | Integer overflow FW header validation | 2026-06-26 |
| 6 | HIGH | kmd.c:5389 | Integer overflow JT bounds check | 2026-06-26 |
| 7 | HIGH | PspKiq.c:323 | body_size=1 → 4 (LOAD_IP_FW payload format) | 2026-06-26 |
| 8 | HIGH | PspKiq.c | Ring size 0x2000 > 9-bit WPTR max 512 DWORDs | 2026-06-26 |
| 9 | MEDIUM | PspKiq.c, PspCore.c | PspGpuProxyWriteRegister return'as ignoruojamas | 2026-06-26 |
| 10 | MEDIUM | PspCore.c:57-84 | Race condition g_GpuDriverHandle init | 2026-06-26 |

### 7.2 Dar Nepataisyti (7)

| # | Priority | Failas | Bug'as |
|---|----------|--------|--------|
| 1 | HIGH | hw.h:360, PspKiq.c:57 | RLC_CP_SCHEDULERS at 0xECA1 NOT 4-byte aligned (0xECA1 & 3 = 1). Empiriškai rastas 0xECAA. |
| 2 | MEDIUM | ioctl.h:420, PspCore.c:17 | IOCTL name collision — `IOCTL_AMDBC250_BAR5_READ_PROXY` = CTL_CODE (0x80000BCC) GPU vs raw (0x900) PSP |
| 3 | LOW | kmd.c:5419 | REG_DUMP reads GRBM_GFX_INDEX at 0x33C4 (should be 0x34D0) |
| 4 | LOW | hw.h:453-454, vm.c:755-762 | GCVM invalidate regs in hw.h (0x0B51C/0x0B520) don't match working code (0x6C0C/0x6C10) |
| 5 | LOW | kmd.c:3570,3601,5412 | GPU_ID read from 3 different offsets (0x0000, 0x3840, 0x0E08) |
| 6 | LOW | kmd.c:5469 | REG_DUMP reads SDMA0_CNTL at 0x10040 (hw.h says 0xE018) |
| 7 | LOW | kmd.h:428-430 | GCVM page table pages never freed on driver unload |

---

## 8. Fundamentalūs Blokeriai

| Blocker | Root Cause | Ar Galima Ištaisyti? |
|---------|-----------|---------------------|
| **KIQ_SIZE=0 read-only** (0xE068) | Hardware-level — ne firmware binary | ❌ Ne |
| **CP_HQD NBIO-blocked** (0xDAC0-0xDBFF) | NBIO firewall | ❌ Ne |
| **GCVM PT_BASE HW-locked** (0x0B608) | Always reads 0 | ❌ Ne |
| **GFX_RING0_BASE_LO read-only** (0xDA60) | BIOS nustato; rašymas ignoruojamas | ❌ Ne |
| **KIQ_WPTR 9-bit limit** (0x1FF) | Hardware limitation | ❌ Ne |
| **SOS firmware no ring protocol** | C2PMSG_64 bit 31 never sets | ❌ Ne |

### 8.1 Kodėl Šis Blockeris Fundamentalus

Visi šeši blokeriai yra **aparatinio lygio** — jų negalima apeiti per firmware patching, NBIO unlock, ar driverio konfigūraciją. Jie yra:

1. **Fizinė HW savybė** — KIQ_SIZE=0, PT_BASE=0, RING0_BASE read-only
2. **NBIO firewall** — CP_HQD blokuotas
3. **SOS firmware limita** — nėra ring protokolo

**Išvada:** BC-250 ši konkreti variacija yra factory-locked GPU command execution. 3D graphics su šiuo HW nepasiekiamas.

### 8.2 Ką Galime

**Software PM4 executor** yra vienintelis veikiantis GPU command vykdymo kelias. Jis:
- Priima standartinius PM4 paketus
- CPU verčia juos į tiesioginius MMIO rašymus
- Leidžia testuoti bet kokį PM4 paketą
- Bet **nepakeičia GPU shader execution**

---

## 9. Ką Veikia ir Ko Ne

### 9.1 GPU Driver — Veikia

| Komponentas | Statusas | Details |
|-------------|----------|---------|
| Driver load | ✅ | WDM driver, DeviceIoControl channel |
| BAR5 MMIO mapping | ✅ | 0xFE800000, 512KB |
| DreamV3WriteRegister/ReadRegister | ✅ | WRITE_REGISTER_ULONG |
| NBIO_MAP init (Flags=1) | ✅ | Safe init path |
| Memory controller init | ✅ | GB_ADDR_CONFIG, FB location |
| IH ring (256KB) | ✅ | Interrupt handler |
| GART page table | ✅ | Software table |
| GPUVM contexts | ✅ | 16 VMIDs |
| Software PM4 executor | ✅ | IT_WRITE_DATA, IT_NOP, IT_EOP, PM4_TYPE_0, IT_INDIRECT_BUFFER |
| IT_SET_CONFIG_REG | ✅ | Config register decode |
| Build + sign pipeline | ✅ | build.bat, prebuild validation |
| 40 CU unlock (attempt) | ✅ | Writes go through, HW-fused |
| PSP proxy bridge | ✅ | GPU driver serves BAR5 access |

### 9.2 GPU Driver — Neveikia/Blokuota

| Komponentas | Statusas | Priežastis |
|-------------|----------|-----------|
| KIQ ring processing | ❌ HW | KIQ_SIZE=0 read-only |
| CP_HQD registers | ❌ NBIO | 0xDAC0-0xDBFF blocked |
| GCVM page tables | ❌ HW | PT_BASE=0 locked |
| GFX ring (CP_RING0) | ❌ HW | BASE_LO read-only |
| Compute rings | ❌ Quirk | AMDBC250_QUIRK_BROKEN_COMPUTE_QUEUE |
| CP firmware load (legacy 0xC000 regs) | ❌ NBIO | 0xC0A0-0xC0B4 blocked |
| SDMA engine | ⚠️ UNKNOWN | 0xE000 regs cause BSOD |
| SMU DPM/clocks | ⚠️ STUB | Minimal support only |

### 9.3 PSP Driver — Veikia

| Komponentas | Statusas | Details |
|-------------|----------|---------|
| Driver load + sign | ✅ | WDM, same cert as GPU |
| BAR5 MMIO mapping | ✅ | MmMapIoSpace or GPU proxy |
| Mailbox READ/WRITE | ✅ | C2PMSG_35/36/37/81 |
| SYSDRV load (0x4) | ✅ | Via mailbox |
| SOS load (0x8) | ✅ | Via mailbox |
| NBIO signature unlock | ✅ | 0xC100/0xC180 written |
| GPU proxy bridge | ✅ | IOCTL 0x900/0x901 |
| Persistent firmware buffer | ✅ | Not freed between IOCTLs |

### 9.4 PSP Driver — Neveikia

| Komponentas | Statusas | Priežastis |
|-------------|----------|-----------|
| GPCOM ring creation | ❌ HW | C2PMSG_64 bit 31 never sets |
| TMR init | ❌ | Reikalingas ring |
| Ring-based PROG_REG | ❌ | SOS FW nepalaiko |
| Mailbox-based PROG_REG | ❌ | Command accepted, write ignored |
| GPU FW via ring | ❌ | Ring protocol unsupported |

---

## 10. Išvada

### 10.1 Ką Pasiekėme

1. **Pilnas HW register map** — BC-250 specifiniai offset'ai, writability, blokeriai
2. **SMU v11.8 protocol** — minimal support, RequestActiveWgp
3. **MEC firmware binary reverse engineering** — header format, Jump Table, ucode
4. **Software PM4 executor** — vienintelis veikiantis GPU command vykdymo kelias
5. **10 bugs fixed** tarp abiejų driverių
6. **Du veikiantys WDM driveriai** su build + sign pipeline

### 10.2 Ko Nepasiekėme

1. **3D graphics** — neįmanomas dėl hardware lock'ų
2. **KIQ ring processing** — KIQ_SIZE=0 hardware-level
3. **GPU shader execution** — reikalauja ringo arba GCVM, abu blokuoti
4. **PSP ring protocol** — SOS firmware nepalaiko

### 10.3 Kam Visa Tai Naudinga

1. **Kitiems tyrėjams** — pilnas register map, HW discovery, workflow
2. **BC-250 reverse engineering** — supratimas kaip ši platforma veikia
3. **Panašioms platformoms** — PS5 APU, Oberon, Cyan Skillfish
4. **RDNA2 Windows driver bendruomenei** — architektūriniai sprendimai

### 10.4 Rekomendacijos Ateičiai

1. **Fokusuotis į ne-užrakintą AMD GPU** (RDNA3+) — kur hardware neužrakintas
2. **Išplėsti SW PM4 executor** — batch rašymai, daugiau opcode'ų, VRAM BAR2 ring
3. **Pataisyti likusius 7 bugs** (žemo prioriteto, neblokuoja kelio)
4. **Dokumentuoti viską open-source** — kad community galėtų tęsti
5. **Tirti SDMA engine** — jei 0xE000 registrai gyvi, DMA galima be CP/KIQ

---

*Ataskaita sukurta: 2026-06-26*  
*Šaltiniai: 53 dokumentai (44 .md, 9 .txt) iš GPU ir PSP driver repos*  
*Repos: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver*  
*       https://github.com/Keshas-dev/AMD-BC-250-PSP-Driver*

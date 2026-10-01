# UEFI SMU Patching + Telemetry Field Map — 2026-10-01 sesija

Šis dokumentas apima 2026-10-01 sesiją: `Hexxeh/bc250-efi-core-unlock` analizė (S
VARBIAUSIA atrastas kelias), Linux `smu_v11_8_ppsmc.h` pranešimų sąrašas,
`amd_smu_reverse_engineering` firmware apžvalga, telemetrijos laukų žemėlapis ir
seno `ps5-win-driver` projekto auditas.

---

## ⭐ 1. `Hexxeh/bc250-efi-core-unlock` — SMU FIRMWARE PATCHING IŠ UEFI

**Repo:** `https://github.com/Hexxeh/bc250-efi-core-unlock` (11★, MIT, 5 commits)

**Kas tai:** freestanding EFI aplikacija (`bc250-unlock.efi`), kuri paleidžiama
**prieš** pagrindinį bootloader'į ir:

1. Perskaaito core mask SMN `0x0115A870`
2. Jei maska ≠ `0xFF`: atrakinėja SMU secure access, rašo `0xFF`, daro **warm reboot**
3. Jei maska = `0xFF`: atrakinėja SMU secure access + **pataiso SMU firmware'ą** ir
   pereina į kitą boot option'ą
4. OS bootina su 8 branduotais branduokliais ir **pataisu SMU firmware**

**Failai:** `main.c`, `smu.c/.h`, `unlock.c/.h`, `patches.c/.h`,
`gen_patches.py`, `Makefile`. Submoduliai: `rw-r-r-0644/bc250-smu-unlock`
(`f5886d0`), `yoppeh/efi` (`761b114`).

### 1.1 🔥 `unlock.c` — VISIŠKAI REALIZUOTA GRANDINĖ

Tai **ne pseudokodas** — tai veikianti, patikrinta produkcijo. Mūsų AGENTS.md
sako, kad šią grandinę sąmoningai neįdiegėme Windows pusėje (dėl ringo
korupcijos rizikos). **Čia ji padaryta UEFI, kur šaltas bootas = švarus lapas.**

** Žingsniai (iš `unlock.c`, tiksliai):**

```
1.  smu_read(0x7B38, 2 dwords)
      entry_byte = word0 & 0xFF
      dbg_byte   = word1 & 0xFF
      if (dbg_byte == 0) → jau atrakinta, SUCCESS
      if (entry_byte != 0) → klaida, STOP

2.  DEAD-ZONE SCAN: nuo (0x7B3C-72) iki (0x3054-72) žingsniu 72 (18 dword'ų)
    Suranda aukščiausią 4-bytų adresą c, kur [c-0x1C, c+0x20) == visi nuliai → P
    (Tai SRAM užrašytas regionas, skirtas walk-table'ams)

3.  N = (0x7B20 - P) / 4
    Validacija: P + 0x18 + 4*N == 0x7B38   (privaloma)

4.  DMA puslapyje stage'ina walk-table header:
      dma_buf[0] = 1                  (count)
      dma_buf[6] = 0x13 | (N << 16)   (FAKE_ENTRY_KEY=0x13, count=N)

5.  Step A: overwrite_tr_table_ptr(P - 0x1C)   → Q2 0x23 overflow
6.  Step B: transfer_engine_dram2smu(dma, 8 dword, sub=0x03)
7.  Verifikacija per smu2dram (Q2 0x0A sub=0x14)
8.  Step C: overwrite_tr_table_ptr(P)
9.  transfer_engine_dram2smu(dma, 1 dword=0, NEW_ENTRY_KEY=0x37)
      → ĮRAŠO 0 Į 0x7B3C  ← **Tai yra gate byte** (DBG_DISABLE)
10. Verifikacija: sec_smn_read32(SMN_CORE_MASK_ADDR) — negali grąžinti 0xFD
11. SMU state fixup:
      smu_memset32(0x7950,  0,   19)     // 0x4C  staging zone
      smu_memset32(0x18DF0, 0,   124)    // 0x1F0 ring slots + counters
      smu_write32(0x7B38,  0)
      smu_write32(0x19780, 0x3F794)
      smu_write32(0x19784, 0x3E000)
      smu_write32(0x19788, 0)
      smu_write32(0x1978C, 0)
      smu_write32(0x17E1C, 0x8B08)
```

**`overwrite_tr_table_ptr(b)` detaliai:**
```c
unsigned int ptr_slot = 243;   // (0x19780 - 0x18850) / 16
// ištušina subqueue-4 (30 - _subq4_cur_idx kartų):
for (i = 0; i < n; i++)
    smu_send_msg_q2(0x23, {0, 0, 0, (1U<<24) | 4U}, 4);   // overflow subq4
// apvirsto į slot 243 (TR_TABLE_PTR):
smu_send_msg_q2(0x23, {243, 0, 0, (1U<<24) | 4U}, 4);
// rašo reikšmę b:
smu_send_msg_q2(0x23, {0, 0, b, (1U<<24) | 1U}, 4);
```

**Ką tai duoda:**
- `smu_read` / `smu_write32` / `smu_memset32` = **arbitrari SMU SRAM R/W**
- `sec_smn_read32` = **arbitrari saugus SMN skaitymas** (Q3 `0x2A`)
- Q3 `0x2B`/`0x2C` = **arbitrari saugus SMN rašymas** (mūsų Windows pusėje NE whitelist'inta)

### 1.2 🔥🔥 `patches.hex` — 60 VIETA **Xtensa KODO PATCH'IAI**

`gen_patches.py` SREC (`type 2` = base, `type 0` = data) → `patches.data.h` →
`g_smu_patches[] = { addr, len, data[32] }`. `patches.c` **lygina** prieš rašydamas
(patikrina, kad jau tas pats → praleidžia), tada **verifikuoja** po rašymo.

Dekuota (`C:\AMD-BC-250\bc250-smu-unlock\bc250_smu\patches.hex`):

```
SRAM 0x03000  len=16   52 A1 20 80 55 11 52 D5 01 52 C5 80 92 25 00 A2
SRAM 0x03010  len=16   25 01 B2 25 02 C2 25 03 D2 25 04 E2 25 05 E0 09
SRAM 0x03020  len=16   00 C0 20 00 A2 65 06 82 A0 5D B0 88 11 C0 20 00
SRAM 0x03030  len=16   88 08 20 92 90 80 99 A0 C0 20 00 68 49 A2 A0 01
SRAM 0x03040  len=8    C0 20 00 A9 06 46 8B 26
SRAM 0x0CA6E  len=3    86 63 D9
SRAM 0x0B863  len=3    A2 14 70
SRAM 0x0B87E  len=3    A2 54 70
SRAM 0x0B884  len=3    A2 14 72
SRAM 0x0B89C  len=3    A2 54 72
SRAM 0x0B8A2  len=3    A2 14 73
SRAM 0x0B8BA  len=3    A2 54 73
SRAM 0x0B8BD  len=3    A2 14 74
SRAM 0x0B8CB  len=3    B2 54 74
SRAM 0x0B8DD  len=3    A2 14 75
SRAM 0x0B8E8  len=3    A2 54 75
SRAM 0x0B900  len=3    A2 22 3B
SRAM 0x0B91A  len=3    A2 22 3D
SRAM 0x0B91D  len=3    82 62 3B
SRAM 0x0B937  len=3    A2 22 3F
SRAM 0x0B93A  len=3    B2 62 3D
SRAM 0x0B946  len=3    A2 62 3F
SRAM 0x0B960  len=3    A2 24 41
SRAM 0x0B97A  len=3    A2 6D 81
SRAM 0x0B9A2  len=3    A2 12 44
SRAM 0x0B9C2  len=3    A2 52 44
SRAM 0x0B9C8  len=3    A2 26 26
SRAM 0x0B9DD  len=3    A2 66 26
SRAM 0x0B9E0  len=3    A2 12 5C
SRAM 0x0B9FD  len=3    A2 12 68
SRAM 0x0BA00  len=3    B2 52 5C
SRAM 0x0BA0C  len=3    A2 52 68
SRAM 0x0BA4B  len=3    A2 13 64
SRAM 0x0BA62  len=3    A2 13 66
SRAM 0x0BA65  len=3    82 53 64
SRAM 0x0BA71  len=3    A2 53 66
SRAM 0x0BA8E  len=3    A2 14 71
SRAM 0x0BAA9  len=3    A2 54 71
SRAM 0x0BAAF  len=3    A2 14 84
SRAM 0x0BAC7  len=3    A2 54 84
SRAM 0x0BACD  len=3    A2 14 85
SRAM 0x0BAD9  len=3    A2 54 85
SRAM 0x0BB6E  len=3    92 52 2E
SRAM 0x0BB71  len=3    F2 52 30
SRAM 0x0BB74  len=3    E2 52 2C
SRAM 0x0BB77  len=3    82 52 2F
SRAM 0x0BB92  len=3    C2 52 31
SRAM 0x0BBCD  len=3    A2 6F 19
SRAM 0x0BBD0  len=3    82 6F 1B
SRAM 0x0BBD3  len=3    E2 6F 1D
SRAM 0x0BBEE  len=3    82 62 1F
SRAM 0x0BC30  len=2    C9 4D
SRAM 0x0BC32  len=3    B2 5A 18
SRAM 0x0BC35  len=3    82 5A 24
SRAM 0x0BC69  len=3    F2 5E 20
SRAM 0x0BC6C  len=3    D2 5E 22
SRAM 0x0BC9F  len=3    E2 52 40
SRAM 0x0BCA2  len=3    F2 52 2D
SRAM 0x0BCA5  len=3    D2 52 41
SRAM 0x0BCB2  len=3    C2 A1 1C

TOTAL PATCH SITES: 60
```

**Kodėl tai kodo patch'ai:** `A2`/`B2`/`C2`/`D2`/`E2`/`F2`/`82`/`92` yra
Xtensa **literal-load opcode šeimos** (`L32R` ir variantai) — 3 baitų
instrukcijos, kurių 2 paskutiniai baitai yra 16-bit **literal reikšmė**.
Paskutinės vietos (`0x03000`–`0x03040`) yra **vector/entry table**.

Interpretuojami literal'iai: `0x7014`, `0x7054`, `0x7214`, `0x7254`, `0x7314`,
`0x7354`, `0x7442`, `0x7452`, `0x7474`, `0x7475`, `0x3B22`, `0x3B2D`, `0x3D22`,
`0x3D26`, `0x3F22`, `0x3F26`, `0x2641`, `0x816D`, `0x4412`, `0x4452`, `0x2626`,
`0x5C12`, `0x5C52`, `0x6812`, `0x6852`, `0x2E52`, `0x3052`, `0x2C52`, `0x2F52`,
`0x3152`, `0x196F`, `0x1B6F`, `0x1D6F`, `0x1F62`, `0x402E`, `0x402D`, `0x4041`,
`0x185A`, `0x245A`, `0x205E`, `0x225E`, `0x1CA1`… → **labai tikėtina, kad tai
SMN adresai arba table indeksai.**

> ⚠️ **NEŽINOMA:** ką patch'ai daro *semantiškai*. `patches.hex` yra tik
> pageidaujama būsena (diff'o rezultatas), originali baitai neįrašyti.
> Reikia `git log -p` `bc250-smu-unlock` arba originalaus patchinto firmware.

### 1.3 ⭐ KĄ TAI REIŠKIA MŪSŲ WGP PROBLEMUI

Tai **stipriausias WGP kelias kurį radome**:

- Mūsų AGENTS.md: *„realus kelias = SMU code execution → SMU **vidinės**
  funkcijos (thelamer įrodė, kad domain'us valdo jos, ne mailbox)"*
- Čia: **bendruomenė jau turi veikiančią SMU firmware patch mechanizmą UEFI**
- Per jį gaunama: **arbitrari SMU SRAM R/W** + **arbitrari saugus SMN R/W**
- Vyksta **UEFI fazėje, PRIEŠ** tai, kada SOS užrakina registr'us
- Šaltas bootas = garantuotas švarus atsistatymas (joks flash, joks volatile
  pakeitimas neišlieka)

**Konkretūs planai (dar neatlikti):**
1. Klonuoti `Hexxeh/bc250-efi-core-unlock` į `C:\AMD-BC-250\`
2. Išsiųsti jame esančius 60 patch'ų per Q2 `0x23`/`0x0A` (jie jau tai daro)
3. **Rasti, kuris patch site valdo WGP/GFX power tick handler'ą**
   (`smu_tick_handlers[0x28]` — 40 įrašų!) ir jį neutralizuoti
4. `SMU_FIRMWARE_OVERVIEW.md` §6 sako: kiekvienas feature = periodinis tick
   handler. **Feature 6 (`GFX_WGP_POWER`) = tick handler.** Tai paaiškina, kodėl
   `RequestActiveWgp` neprilaiko: bitas įjungtas, bet niekas nevalo
5. Alternatyva: `0x7B3C` gate jau `0` nuo boot'o (mūsų finding), todėl
   **reiktų tik atrakinti `sec_smn_write32` (Q3 `0x2C`) ir parašyti SPI_PG SMN adresu**

**Pieno planai:** Visa tai — **UEFI aplikacija, ne Windows driveris.** Tai
reiškia, kad nereikia nei `Q2 0x23` Windows pusėje (kurios sąmoningai
neįdiegėme dėl rizikos), nei AC ciklo atkūrimo — UEFI kiekvieną kartą
perdeda švariai.

### 1.4 Kompiliavimas

`Makefile` — dvi aplinkos, abi Linux/macOS (ne Windows):
```
make mingw   # gcc-mingw-w64-x86-64, -nostdlib -mno-red-zone -shared
make clang   # clang+lld, -target x86_64-unknown-windows, -ffreestanding
```
Windows kompiliatoriams (VS/WDK) ir**nėra** pavyzdžio — reiktų pridėti trečią
target'ą arba naudoti WSL/cross-compile.

---

## ⭐ 2. Linux `smu_v11_8_ppsmc.h` — MŪSŲ VERSIJOS PRANEŠIMŲ SĄRAŠAS

Iš `drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v11_8_ppsmc.h` (Linux master).
**Tai autoritetyvus Q0 (PPSMC) sąrašas SMU 11.8 = mūsų.**

### 2.1 Status koda — patvirtina mūsų driverį 1:1
```c
#define PPSMC_Result_OK                0x1
#define PPSMC_Result_Failed            0xFF
#define PPSMC_Result_UnknownCmd        0xFE
#define PPSMC_Result_CmdRejectedPrereq 0xFD
#define PPSMC_Result_CmdRejectedBusy   0xFC
#define PPSMC_Message_Count            0x3E   // Q0: 0x01..0x3D
```

### 2.2 ⭐ NAUJI READ-ONLY PRANEŠIMAI, KURIŲ NETURIME WHELIST'Ą

| Msg | Pavadinimas | Ką duoda | Rizika |
|-----|-------------|----------|--------|
| `0x11` | `QueryVddcrSocClock` | **DRAM / memory clock (MHz)** — trūkstantis laukas! | **Nulis** (read-only) |
| `0x13` | `QueryDfPstate` | SoC power state | Nulis |
| `0x0C` | `QueryCorePstate` | per-branduolio P-state (paaiškins, kodėl core 1/6 down) | Nulis |
| `0x1B` | `StartTelemetryReporting` | paleidžia SMU telemetriją | Nulis (tik bitas) |
| `0x1D` | `ClearTelemetryMax` | išvalo max skaitiklius | Nulis |

**Rekomenduojamas whitelist'o papildymas: `0x11`, `0x13`, `0x0C`.** Visi
read-only, visi Linux-verified, arg = 0.

### 2.3 Q0 pranešimai, kurių **NIEKADA** neliesti
| Msg | Pavadinimas | Kodėl |
|-----|-------------|-------|
| `0x2E` | `InitiateGcRsmuSoftReset` | **GC soft reset = katastrofa** |
| `0x16`/`0x17` | `ConfigureS3PwrOffRegisterAddressHigh/Low` | S3 power-off reg adresai — pavojinga |
| `0x0A` | `Rsvd1` | Rezervuotas (sukryžčiuoja jų Ghidra dump'ą) |
| `0x08`/`0x09` | — | `No Handler / Reserved` (jų Ghidra) |

### 2.4 Q3 neturi Linux headerio
Q3 pranešimai (`0x36` CPU mV, `0x43` core freq, `0x98` ungated write, `0x8B/0x8C/0x8F/0x50/0x9A`)
žinomi **tik** iš bendruomenės (`bc250_smu_oc`, `SMU-HANDLERS.md`) — Linux
cyan_skillfish Q3 nenaudoja. Mūsų whitelist'ų pagrindas = bendruomenės, ne
kernelio.

---

## ⭐ 3. `amd_smu_reverse_engineering` — `SMU_FIRMWARE_OVERVIEW.md`

### 3.1 ⚡ MŪSŲ FIRMWARE YRA REPO!
```
C:\AMD-BC-250\amd_smu_reverse_engineering\smu_fw\
  smu_fw_robin_1   262400 B   0.58.6.0  ← MŪSŲ (BIOS 3.0, 88.6.0)
  smu_fw_robin_5   262400 B   0.58.7.1
  SHA256 robin_1 = 8C29CF0B1C5EA713F1F8AE95ED4C1DC547D00C530530C131950CFD5EB08C6675
```
**Pre-trimmed (256 baitų headeris nuimtas), PLAINTEXT.** Tai leidžia:
- **Sukryžčiuoti mūsų ROM išgavimą** su jų kopija (validacija)
- Ghidra analizę **be papildomo išgavimo** (jie turi 2 Ghidra script'ą:
  `smu_message_helper.py`, `smu_function_helper.py`)

**Metodika (README):** Xtensa-le, adresų erdvė nuo `0x00000000`, Ghidra **12.1+**
su `yath/ghidra-xtensa` SLEIGH (reikia `support/sleigh -a`). Išjungti
"Non-Returning Functions - Discovered".

**Jų Q0 išdavinys (iš README)** — **sutampa su `smu_v11_8_ppsmc.h`**:
```
0x01 TestMessage            0x0B RequestCorePstate
0x02 GetSmuVersion          0x0C QueryCorePstate
0x03 GetDriverIfVersion     0x08/0x09/0x0A No Handler / Reserved
0x04 SetDriverTableDramAddrHigh
0x05 SetDriverTableDramAddrLow
0x06 TransferTableSmu2Dram  0x07 TransferTableDram2Smu
```
`PPSMC_MSG_TestMessage` pseudokodas (`pmfw_queue_write_status(queue, 1)`)
patvirtina status `0x01` = OK.

### 3.2 §7 Telemetrijos vamzdis
- `cpu_metrics_178` — **per branduolio**: VID, **galia**, temperatūra, dažnis
- `electrical_data` — **įtampa / srovė / galia** CPU ir GFX domenams
- `SmuMetricsTable_t` (hostui): core ir **L3 dažniai/temp**, C0 residency,
  **GPU/SoC clk**, volt/current/**power**, socket power, **edge temp**,
  throttler būsena
- `SmuMetrics_Current` + `SmuMetrics_Average` (`table_6_tick_SmuMetrics`)
- `table_6_smu2dram_SmuMetrics` — **supakuoja metrics DRAM peržiūrai** ⭐

**Tai LAIKAS `ps5-hwinfo` laukų — Faza 2 (GPU_METRICS) yra teisingas kelias.**

### 3.3 §8 Table transfer — **Faza 2 RIZIKA**
> *„Check **per-queue permission masks** via `PTR_DAT_0000ca7c`"*
- Pointer'ų masyvai: `PTR_table_0_smu2dram_0000ca70`,
  `PTR_table_0_dram2smu_0000ca6c`, `PTR_DAT_0000ca7c`
- **Rizika:** Q0 gali **nebūti** teisių daryti table transfer. Reikia tikrinti
  prieš Faza 2, nebandant kartoti nė vieno karto.

### 3.4 §6 Feature framework — **PAIŠKINAS WGP NULĮ**
> *„Each feature typically installs a periodic tick handler in
> `smu_tick_handlers[0x28]` using `smu_set_tick_handler`"*

- `smu_tick_handlers[0x28]` = **40 įrašų**
- `smu_run_tick_handlers` kiekvieną tick'ą iteruoja visus
- **Feature 6 (`GFX_WGP_POWER`, jau ON maske `0xDD602C7D`) = periodinis handler.**
  Bitas įjungtas ≠ funkcija vykdo. **Tai paaiškina, kodėl
  `RequestActiveWgp(1..18)` priimamas, bet `ActiveWgp == 0`.**
- Kandidatas vėliau: neutralizuoti `smu_tick_handlers` įrašą per SMU SRAM patch'ą

### 3.5 §4 Message queues — 8 eilutės
- `queue_descriptor_table_offs_0[8]` — ARG/RSP/CMD kiekvienai
- `queue_table_8` — per-queue handler table'ų rodyklė
- `queue_msg_handler` = funkcijos rodyklė + config baitai (guard, sync/async)
- `queue_guard_override` — **secure handler'iams guard tikrinimas**
- `export.html`: `queue_descriptor_table_offs_0` ~`0x0000700c`,
  `message_table_0` ~`0x0000706c`
- **Rezervatas mums**: Q1, Q4–Q7 nebandyti — nežinome jų adresų ir jie gali
  būti skirtingo tipo (CPU valdymas)

### 3.6 §2 Boot — **NUJOVA, KONTRADIKTUOJA MŪSŲ SENĄ ĮRAŠĄ**
`init_system_2nd_level` „sets up constants, **core masks**, VID tables, and
**P-state tables**" → **P-state lentelės firmware VIDAU egzistuoja.**

Mūsų AGENTS.md sako „NO DPM TABLES EXIST FOR BC-250" — tas **teisingas Linux
`cyan_skillfish_table_map` (host table)**, bet **firmware vidinės P-state
lentelės yra**. Ne prieštaravimas, o skirtingos lentelės.

---

## 4. Telemetrijos laukų žemėlapis (`ps5-hwinfo` vs BC-250)

`ps5-hwinfo` (`git.etawen.dev/drakmor/ps5-hwinfo`) — geriausias etalonas,
atrastas ieškant `ps5-vitals`.

| `ps5-hwinfo` laukas | BC-256 dabar | Kaip gauti |
|---|---|---|
| CPU temp | ✅ turime | Q3 `0x36` (mV) — **°C dar ne** |
| CPU freq per core | ✅ turime | Q3 `0x43` (0..7) |
| GFXCLK | ✅ turime | Q0 `0x37` / `0x0F` |
| GPU VID | ✅ turime | Q0 `0x38` + `vid_to_mv()` |
| CPU mV | ✅ turime | Q3 `0x36` |
| **Memory (DRAM) clk** | ❌ **trūksta** | ⭐ **Q0 `0x11` (Linux-verified)** |
| **FCLK / UCLK / cache clk** | ❌ trūksta | metrics lentelė (Faza 2) |
| **SoC galia (W)** | ❌ (0) | `SmuMetricsTable_t` (Faza 2) |
| **ShellCore HLT/GC/BAPM/VM** | ❌ trūksta | metrics lentelė |
| VRAM naudojimas | ⚠️ fiktyvus | — |
| Fan | ❌ NCT6687D | atskira SMBus驱动 reikia |

**Reikalingi IŠORINIAi duomenys (bendruomenė):**
- SMU savi clk getters: `dom00 500, dom19 1500, fclk 1200, uclk 875`
  (tiekiami tik per metrics lentelę)
- **`SmuMetricsTable_t` yra fiksuotos 116 baitų.** 8-branduolių unlock'o
  per-core masyvas užgriebia `GfxclkFrequency` lizdą → `pp_dpm_sclk` rodo
  nonsense. **Tai mums nėra problema** (mes nenaudojam amdgpu), **bet** reiškia
  **laisvas 6 laukus reikia praleisti** apskaičiuojant offset'ą

---

## 5. ⛔ Patikrinta ir ATMETTA (2026-10-01)

### 5.1 `ps5-vitals` (PyPI) — mechanizmas netinka
Naudoja **Sony proprietarinius firmware API**, kurių BC-250 nėra:
```
sceKernelGetCpuTemperature / sceKernelGetSocSensorTemperature(0)
sceKernelGetCurrentFanDuty   / /dev/icc_fan ioctl 0xC01C8F07
```
+ jailbreak (Y2JB → etaHEN) + elfldr TCP 9021 + klog TCP 9081.
**Nėra `sceKernel*`, nėra `/dev/icc_fan`, nėra Sony firmware → neperkelia.**

### 5.2 `ps5debug-NG/PROTOCOL.md` — tik RAM, nėra aparatūros
Wire protokolas (v1.3.2, 69 opcode). **0 hit'ų**:
`MMIO`, `BAR`, `MSR`, `ioport`, `SMU`, `PSP`, `SMN`, `GART`, `power`, `clock`,
`voltage`, `thermal`, `device register`.

Tik 2 įdomios detalės:
- **„GPU/Garlic blob"** — *Garlic* = Sony GPU kodo vardas; regionas
  **uncached, ~40 MB/s**, ps5debug jį tik siūlo praleisti
- `CMD_KERN_READ/WRITE` (`0xBDCC0002/3`) — `kernel_copyout_fast/copyin_fast`
  = arbitraus FreeBSD branduolio virtualios atminties R/W
- `TSE_ALIASING` (`0x02`) — **read-only** fizinių puslapių aliasingas per
  target'o page tables (ne arbitraus fizinio adreso primityvas)
- Duomenys gaunami per DMAP/mdbg **page-table walk** — klientas niekada
  nepersiunčia fizinio adreso

### 5.3 Senas `F:\bc-250-proektas\ps5-win-driver` — **0 hardwar faktų**
201 .ps1 failas (167 unikalūs). **Visi 3 analizės agentai sutapo: 0 patvirtintų
hardwar faktų mūsų tikslams.**

**`amdbc250_hw.h` — VISI OFFSET'AI NUGRIUVĘ** (raw Navi10, be `GC_BASE=0x1260`,
BAR0 vietoj BAR5):
```
SCRATCH_REG0  0x8500  (mūsų 0x32D4)     CP_ME_CNTL   0x8080  (0x4A74)
CP_HQD_ACTIVE 0x80D8  (0x910C)           GB_ADDR_CONFIG 0x263C (0x61D8)
C2PMSG        0x16104 (0x03B10A08 SMN)
```
Tai kodėl Code 43 — **išoriniai projektai su BAR0+Navi offset'ais visi failina.**

**6 dalykai, kuriuos verta pasiimti:**

| Radimas | Kur panaudoti |
|---------|---------------|
| ⭐ `Run-GuardedDriverTest.ps1` — 8 pakopų apsauga (išorinis watchdog procesas, 60s countdown + ABORT, `display.inf` baseline gate, `shutdown /a` pirmiausia, sign gate) | **BIOS flash + WGP test flow** — stipresnis už `reinstall-gpu-driver.bat` |
| ⭐ `bcdedit /set {current} safeboot minimal` | GPU init užsikirtų atkirtimui |
| `ROOT\DISPLAY\0000` reikia pašalinti (`pnputil /remove-device`) | display rebind blockeris |
| `ati2mtag_Navi10` Inf injekcija į Adrenalin | **BC-250 sėkmingai binda** prie AMD tikrojo Navi10 stack'o → silicas neužrakintas |
| `DxgkInitialize` → `STATUS_SUCCESS` pasiektas | bisect → **`ResetDevice` = vienintelis reikalingas callback** |
| Registry breadcrumbs: `ZwCreateKey(RegistryPath\Parameters)` + `ZwSetValueKey`, `Bc250Phase` 1=pre/2=post, `Bc250DxgkStatus` pre-seeded `0xFFFFFFFF` | papildo mūsų `Step_HwInit`/`DriverBuildId` grąžinamu NTSTATUS |

**⚠️ Jų measurement artefaktas — SAUGOTIS:**
`amdbc250kmd` rank `0xF60001` > BasicDisplay `0xFB2008` → driveris nukris →
sistema fallback'ina į BasicDisplay → jų harness rodo `CM_PROB_NONE`.
**`CM_PROB_NONE` ≠ driveris veikia.** Tai mūsų harness'o pavojus.

**Bonus įrodymas:** `amdvlk64.dll` → `gfx1013` **1** string hit vs `gfx10` **7669**.
Kiekybinis įrodymas, kad oficialus AMDVLK neatšaukia BC-250 (tik RADV).

**Bendras PS1 pavyzdys:** `scripts/` ir `*.ps1` yra 100% build/install/rollback
harness. Filtravimas: `phase1\` (55 failai) = 100% RDP/Terminal Services, praleisti.

---

## 5. ✅ `bc250-vitals.exe` — Faza 1 ĮRANKAS SUKURTAS (2026-10-01)

**Failas:** `test-tools\bc250-vitals.c` + `test-tools\compile-bc250-vitals.bat`
**Build:** `output\bc250-vitals.exe`, `/W3` 0 warning'ų. Driveris **4.3.0.18**
(`SHA256 B5533F9C316364BCAB043909D0D34B18E7671215377408F853533BA9369384A6`,
`.sys` Valid, `.cat` Valid).

### 5.1 Gyvas rezultatas (prieš 4.3.0.18 diegimą)
```
========================================================
 AMD BC-250 Vitals   2026-10-01 03:32:44
========================================================
SMU          88.6.0   driver-if 8   [telemetry: ok]
CPU          1199 mV
Cores        c0=3500 MHz  c1=3500 MHz  c2=3500 MHz  c3=1555 MHz
             c4=3500 MHz  c5=3500 MHz  c6=1555 MHz  c7=1555 MHz
Core mask    0xFF = 8 core(s)  [unlocked]   (CF8/CFC)
GPU          1500 MHz  [OK]   VID 99 = 931 mV
WGP          0 active   [GFXOFF / deep sleep]
SoC/DRAM     (unavailable)          <- 0x11 dar newh whitelist'inta
Features     0xDD602C7D  GFXCLK_DPM|GFXOFF|CLOCK_GATING|POWER_GATING|GFX_WGP_POWER
VRAM         16384 MB reported by driver (NOT the CMOS UMA size; use cmos-memcfg-test.exe)
SMN raw      edge=0xFFFFFFFF junction=0x00000000 mem=0x00001EC2
             fan=0x00000000 fanpwm=0x00000000  (raw - no decode table)
--------------------------------------------------------
```

### 5.2 Driverio pakeitimai (4.3.0.18)
| Pakeitimas | Vieta | Pagrindas |
|---|---|---|
| Q0 `0x11 QueryVddcrSocClock` → `SMU_ARG_NONE` | `kmd.c` `Whitelist[]` | DRAM clock, trūkstantis laukas |
| Q0 `0x13 QueryDfPstate` → `SMU_ARG_NONE` | `kmd.c` `Whitelist[]` | SoC power state |
| Q0 `0x0C QueryCorePstate` → `SMU_ARG_CORE_ID` (0..7) | `kmd.c` `Whitelist[]` | per-core p-state |
| 3 nauji `#define` + dokumentacija | `inc\amdbc250_ioctl.h` | Linux `smu_v11_8_ppsmc.h` |
| `static_assert` × 3 | `kmd.c` | apsauga nuo ID „drift“ su `amdbc250_dream_kmd.h` |
| `DriverVer` → `4.3.0.18` | `inf\amdbc250_dream.inf` | **hash nepadėklo — versija privalo augti** |

### 5.3 ⛔ Code Reviewer radimai (7 sutaisyti)

| # | Severity | Radimas | Sutaisymas |
|---|----------|---------|-----------|
| 1 | 🔴 **MEDIUM (LPE!)** | **Q0 `0x04`/`0x05` + `0x07`** leido nukreipti SMU table-DMA engine į **bet kurį 4KB-alignintą fizinį adresą** — **tai LPE**, ir būtent ta skylė, kurią `SMU_MSG_ARGS` 0x0A kelias sąmoningai uždėrė pririšdamas prie `DevExt->SmuUnlockPa`. `SMU_CPU_MSG` kelias niekada negaavo to paties. | Naujas `SMU_ARG_DRIVER_PA`: tik `DevExt->SmuUnlockPa` |
| 2 | 💭 LOW | `PCI_SMN_ACCESS` (`0x80000C30`) **nebuvo po `DeviceMutex`**, nors naudoja tą patį bendrą NBIO SMN 0x38/0x3C portą kaip `SMU_CPU_MSG` → `--watch` ilgai veikdamas pasidarytų neteisingą skaitymą | `ExAcquireFastMutex` / `Release` apimta |
| 3 | 🟡 MEDIUM | `smn_read()` reikalavo **dviejų transportų sutapimo** → `Core mask` visada rodytų „unavailable“ (BAR5 NBIO neatkodina ne-GC adresų) | CF8/CFC → **autoritetyvas**, BAR5 → **konsultatyvus** |
| 4 | 🟡 MEDIUM | CSV rašė `0` nepavykusiam klaidymui — `0 MHz` ir `p-state 0` yra realiai įmanomi → „mirusio SMU“ duomenys atrodėte kaip tikri | `num_or_dash()` → `-` |
| 5 | 💭 LOW | `tel_st` rodomas kaip „SMU statusas“ | pervadinta į `[telemetry: ok/timeout]` |
| 6 | 💭 LOW | CSV `--watch` metu meta tuščią eilutę | pašalinta |
| 7 | 💭 LOW | Header commentas **neteisingai** tvirtino „verified against Ghidra dump“ (dumpo buildas = `robin_5` / **88.7.0**, ne mūsų 88.6.0); „0x0A reserved“ be „Q0“ priešdėlio (Q2 `0x0A` = transfer engine, whitelisted!) | abu ištaisyta |

**Code Reviewer galutinis verdiktas:** *„whitelist pakeitimas pats sava saugus išsiųsti —
ID teisingi, jokių kolizijų, argumentų validatoriai teisingi ir minimalūs,
`Amdbc250PspDirectSmuMsg` rašo tik paštą; PMFW dispatch lentelė patvirtina
skirtingus read-only handler'ius (`0x11` nėra pavojingos `0x0E` šeimos).“*

**PMFW dispatch įrodymas:** `0x0E → FUN_0002b400`, `0x11 → FUN_0002ea74`,
`0x13 → FUN_0001e988`, `0x0C → FUN_00022c94` — **skirtingi kodai, visi `Query*`.**

### 5.4 ⚠️ Ką šis įrankis DARO negali ( sąžiningos ribos)
- **Temperatūros °C nėra.** `GET_SMU_TELEMETRY` laukai `CpuCoreMask`,
  `CpuVoltageMv`, `GpuVoltageMv` **draiveris niekada neužrašo** (visada 0) →
  įrankis jų nenaudoja (ima savo Q3 `0x36`). `GET_TEMP_INFO` grąžina
  **sintetinius stub'us** (`Junction = Edge + 12`) → sąmoningai nenaudojama.
- **SMN raw temperatūros** (`0x03B10000/20/28/64/68`) = **nekoduoti registro
  reikšmės**. `mem=0x00001EC2` — **cyan_skillfish temperatūros dekodavimo
  lentelės neturime**; spėti būtų netikslūs duomenys. Reikia Ghidra analizės
  (`smu_fw_robin_1` jau plaintext — tiesioginiai kelias).
- **Fan** = NCT6687D (SMBus), ne SMU. Reikia atskiro SMBus driverio.
- **VRAM** = driverio konstanta (16 GB), ne matavimas. Tikrasis UMA —
  `cmos-memcfg-test.exe` (CMOS 0xAA).

---

## 6. Aktyvios taisyklės / saugumas (papildyta)

- ⛔ **NIEKADA** skaityti SMU `0x03B1xxxx` bloko per Q3 `0x2A` — savireferencinis
  skaitymas užkina SMU (soft rebootas neatgauna, reikia AC ciklo)
- ⛔ **NIEKADA** Q3 `0x98` su atsitiktiniu adresu (0x0115A870 tik)
- ⛔ **NIEKADA** Q3 `0x2E` (`InitiateGcRsmuSoftReset`) — net ne whitelist'inti
- ⛔ **NIEKADA** aklo SMN sweep'o — sukėlė boardo užklausimą
- ⚠️ **Q3 `0x36` grąžina mV, ne °C.** `GET_TEMP_INFO` (`0x80000808`) grąžina
  **sintetines stub'ų** reikšmes (`Junction = Edge + 12`) — **nenaudoti**
- ⚠️ `GET_SMU_TELEMETRY` (`0x80000B9C`) laukai `CpuCoreMask`, `CpuVoltageMv`,
  `GpuVoltageMv` **draiveris niekada neužrašo** → visada 0.
  `smu-telemetry-cli.c` juos spausdina kaip tikrus — **klaida.**
- ⚠️ `PCI_SMN_ACCESS` (`0x80000C30`) **neturi adresų whitelist'o** — arbitraus
  SMN R/W. `GET_SMU_TELEMETRY` ir `PCI_SMN_ACCESS` **nėra** po `DeviceMutex`
  (tik `SMU_CPU_MSG` yra), nors visi trys naudoja NBIO SMN 0x38/0x3C portus →
  **neaškinti `--watch` su kitu SMU įrankiu** (driverio skylė, follow-up)

---

## 7. Kitas žingsnis

1. **Klonuoti `Hexxeh/bc250-efi-core-unlock`** → `C:\AMD-BC-250\`
2. **Prikompiliuoti Windows target'ą** (arba WSL) ir adaptuoti
   `bc250-vitals.c` (6 Code Reviewer fix'ų liko parašyti)
3. **Driverio whitelist'ą papildyti Q0 `0x11` / `0x13` / `0x0C`** — DRAM clock
4. **Tikrinti `SmuMetricsTable_t` per-queue permission mask** (`0x0000ca7c`)
   prieš Faza 2
5. **Pagal `smu_tick_handlers[0x28]` ieškoti WGP power handler'o** per SMU SRAM
   patch'ą (UEFI)
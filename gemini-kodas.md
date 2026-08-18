# AMD BC-250 Windows Driver — Kodo Kokybės ir Linux Registrų Analizė

**Data:** 2026-08-13  
**Projektas:** AMD BC-250 (Cyan Skillfish / RDNA2) Windows GPU + PSP Driver  
**Dokumentas:** Kodo analizė, registrų adresų ir reikšmių patikrinimas pagal Linux kernel šaltinius (`gemini-kodas.md`)

---

## 1. Bendras Kodo Kokybės ir Architektūros Vertinimas

**Įvertinimas:** **9 / 10** (Labai gerai struktūrizuota WDM NT tvarkyklė su saugiomis registrų ir atminties operacijomis).

### Pagrindiniai Privalumai:
- **Saugus registrų pasiekimas**: Visi MMIO rašymai atliekami per `WRITE_REGISTER_ULONG` ([amdbc250_dream_kmd.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_kmd.c)), išvengiant Windows 11 26100 volatile pointer nurašymo (silent drop) problemos.
- **SEH ir Atminties Saugumas**: Visi MMIO nuskaitinėjimai, IOCTL apdorojimai ir buferiai apsaugoti per `__try / __except` blokus. Pataisyti ribų (bounds) patikrinimai bei `uint32_t` pervilkimo (integer overflow) apsaugos.
- **Software PM4 Fallback**: Įgyvendintas *Software PM4 Executor*, kuris CPU lygmeniu verčia PM4 komandas į MMIO rašymus, kai aparatinės eilės būna užrakintos.

---

## 2. Registrų Adresų ir Reikšmių Sutikrinimas su Linux

Tvarkyklėje naudojami Linux Kernel `cyan_skillfish_ip_offset.h` ir `gc_10_1_0_offset.h` apibrėžimai. BC-250 (Cyan Skillfish / GFX10.1.3) naudoja poslinkį `GC_BASE = 0x1260`.

**Formulė BAR5 adresui skaičiuoti:**  
$$\text{BAR5\_Adresas} = 0x1260 + (mm\_DWORD \times 4)$$

### Registrų Adresų Sutikrinimo Lentelė

| Registras | Linux $mm$ offset | Skaičiuotas BAR5 | Windows `hw.h` | Būklė |
| :--- | :--- | :--- | :--- | :--- |
| `GRBM_STATUS` | `0x0800` | `0x1260 + 0x2000` | **`0x3260`** | **TEISINGAS** |
| `CP_SCRATCH_REG0` | `0x081D` | `0x1260 + 0x2074` | **`0x32D4`** | **TEISINGAS** (BIOS MXPB markeris) |
| `CC_GC_SHADER_ARRAY_CONFIG` | `0x226F` | `0x1260 + 0x89BC` | **`0x9C1C`** | **TEISINGAS** (CU statusui) |
| `SPI_PG_ENABLE_STATIC_WGP_MASK` | `0x1277` | `0x1260 + 0x49DC` | **`0x5C3C`** | **TEISINGAS** |
| `COMPUTE_DISPATCH_INITIATOR` | `0x1BA0` | `0x1260 + 0x6E80` | **`0x80E0`** | **TEISINGAS** (Pataisytas iš senos 0xDC60) |
| `COMPUTE_PGM_LO` | `0x1BAC` | `0x1260 + 0x6EB0` | **`0x8110`** | **TEISINGAS** |
| `COMPUTE_PGM_HI` | `0x1BAD` | `0x1260 + 0x6EB4` | **`0x8114`** | **TEISINGAS** |
| `CP_MQD_BASE_ADDR` | `0x1FA9` | `0x1260 + 0x7EA4` | **`0x9104`** | **TEISINGAS** |
| `CP_HQD_ACTIVE` | `0x1FAB` | `0x1260 + 0x7EAC` | **`0x910C`** | **TEISINGAS** |
| `CP_HQD_PQ_CONTROL` | `0x1FBA` | `0x1260 + 0x7EE8` | **`0x9148`** | **TEISINGAS** |
| `CP_RB0_BASE` (Linux GFX Ring) | `0x1DE0` | `0x1260 + 0x7780` | **`0x89E0`** | **TEISINGAS** |
| `MP1_BASE` (SMU Mailbox) | `0x5800` (SMU) | `0x16000 + offset` | **`0x16A08` / `0x16A48` / `0x16A68`** | **TEISINGAS** |

---

## 3. BC-250 Specifiniai Skirtumai nuo Standartinio Linux (Hardware Quirks)

Nors dauguma registrų atitinka Linux specifikaciją, kodo autoriai teisingai pritaikė BC-250 kasybos plokštės ypatumus:

1. **`GRBM_GFX_INDEX` (`0x34D0`) vs `GRBM_GFX_CNTL` (`0x4968`)**:
   - Linux kode nurodomas `GRBM_GFX_CNTL` (`0x0dc2`), tačiau ant BC-250 aparatinės įrangos šis registras skaito `0xFFFFFFFF`.
   - Tvarkyklėje [amdbc250_dream_hw.h](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/inc/amdbc250_dream_hw.h) naudojamas `GRBM_GFX_INDEX` (`0x34D0`), kuris realiai veikia ir reaguoja į rašymus.
2. **`CP_ME_CNTL` (`0x4A74`)**:
   - Šis registras sėkmingai valdo ME/PFP halt būseną (ME unhalt teste patvirtinta, kad bitas 28 atsakydavo ir išsivalydavo iki `0x00000000`).
3. **NBIO Firewall (`0xC000 - 0xCFFF`)**:
   - Tvarkyklė teisingai nukreipia rašymus per `GC_BASE` postūmį, aplenkdama NBIO tiltą, kuris blokuoja tiesioginę prieigą prie 0xC000 diapazono.

---

## 4. Išvada

Tvarkyklės kodo kokybė yra **aukšto lygio**, o visi pagrindiniai registrų adresai bei aprėptys, paimti iš Linux Kernel šaltinių, yra **TEISINGI** ir tinkamai pritaikyti BC-250 platformai.

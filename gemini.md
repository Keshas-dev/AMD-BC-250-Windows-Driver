# AMD BC-250 Windows Driver — Projekto Analizė ir Būklės Ataskaita

**Data:** 2026-08-13  
**Projektas:** AMD BC-250 (Cyan Skillfish / RDNA2) Windows GPU + PSP Driver  
**Plokštė:** AMD BC-250 Mining Card (Oberon / PS5 APU variantas)  

---

## 1. Projekto Apžvalga ir Įgyvendinta Architektūra

### A. Kernel Mode Driver (KMD)
- Tvarkyklė [amdbc250_dream_kmd.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_kmd.c) veikia WDM IOCTL režimu (`\\.\AMDBC250DreamV43`).
- MMIO BAR5 (0xFE800000) registrai pasiekiami per `WRITE_REGISTER_ULONG` saugią prieigą ([amdbc250_dream_kmd.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_kmd.c)).
- Implementuota 30+ IOCTL handlerių: registrų skaitymas/rašymas, atminties valdiklio (`GB_ADDR_CONFIG`) konfigūravimas, GART ir GCVM puslapių lentelės.
- Įgyvendintas **Software PM4 Executor** ([amdbc250_dream_kmd.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_kmd.c)), CPU lygmeniu verčiantis PM4 paketus į MMIO registrus (`IT_WRITE_DATA`, `IT_NOP`, `IT_SET_CONFIG_REG`, `IT_INDIRECT_BUFFER`).

### B. PSP Driver ir Firmware Įkėlimas
- Gretimasis driveris `PspDriver.sys` ir [amdbc250_psp.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_psp.c) valdo PSP mailbox protokolą.
- PSP mailbox komanda `0x06` (`GFX_CMD_ID_LOAD_IP_FW`) sėkmingai užkrauna ME, PFP, CE, MEC ir RLC mikrokodus.
- [amdbc250_dream.inf](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/inf/amdbc250_dream.inf) automatiškai diegia mikrokodo failus (`Sysdrv.bin`, `Sos.bin`, `Smu.bin`, `cyan_skillfish2_*.bin`) į `C:\Windows\System32\drivers\bc-250\`.

### C. Kiti Komponentai
- **Valdymo įrankis**: [bc250cc.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/apps/bc250-control-center/bc250cc.c) (Win32 GUI Control Center) SMU telemetrijai, dažniams ir registrams stebėti.
- **Kompiliavimas**: [build.bat](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/build.bat) automatiškai surenka ir pasirašo tvarkykles sertifikatu `AMD-BC250-Signer`.

---

## 2. Ko Projektai Šiuo Metu Trūksta

### A. Kernel Mode Driver (KMD) ir Aparatūra

1. **SMU (System Management Unit) Firmware ir Laikrodžių (Clocks) Aktyvavimas**:
   - **Būklė**: [amdbc250_dream_power.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_power.c) turi tik minimalius SMU v11.8 stub'us.
   - **Trūkumas**: Trūksta SMU firmware (`cyan_skillfish2_smc.bin`) užkrovimo ir SMU handshake sekos, kuri įjungtų GFX IP bloko taktinius dažnius (clock gating) bei maitinimą (power gating).
   - **Poveikis**: Nors ME unhalted ir MEC firmware užkrautas, aparatiniai ringai (RPTR) nejuda, nes GFX/Compute varikliai negauna laikrodžio signalo. Linux `amdgpu` tvarkyklėje tai atliekama per SMC v88.7.1 handshakes.

2. **VBIOS (ATOM BIOS) Nuskaitymas iš ACPI VFCT Lentelės**:
   - **Būklė**: [amdbc250_dream_vbios.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_vbios.c) apibrėžia tik bazines struktūras.
   - **Trūkumas**: Trūksta ATOM BIOS parsintuvo, kuris įkrovimo metu ištrauktų VBIOS iš ACPI VFCT lentelės ir įvykdytų VBIOS komandų lenteles (ASIC init, PowerPlay lentelės, dažnių lentelės).

3. **WDDM Miniport ir DCN 2.0.1 (Display Engine) Įgyvendinimas**:
   - **Būklė**: Tvarkyklė veikia kaip WDM IOCTL fallback; Windows rodo "Microsoft Basic Display Adapter".
   - **Trūkumas**: Trūksta pilnos WDDM 2.0+ miniport integracijos bei DCN 2.0.1 ekrano valdiklio inicializacijos (CRTC, HDMI/DisplayPort išvestys, EDID nuskaitymas).

4. **Aparatinis SDMA (System DMA Engine) Driveris**:
   - **Būklė**: SDMA firmware įkraunama per PSP mailbox, tačiau SDMA registrai (`0xE000`) nevaldyti saugiai.
   - **Trūkumas**: Trūksta veikiančio SDMA žiedo (ring buffer) ir IOCTL sąsajos greitam duomenų perkėlimui tarp sistemos RAM ir VRAM (BAR2).

### B. User-Mode Driveris (UMD) ir Grafinių API Sloksnis

1. **Direct3D (D3D11 / D3D12) UMD Realizacija**:
   - **Būklė**: [amdbc250_umd_v46.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/umd/amdbc250_umd_v46.c) turi DDI eksporto funkcijas, tačiau dauguma grąžina `E_NOTIMPL`.
   - **Trūkumas**: Trūksta tekstūrų/buferių alokacijos, Pipeline State Objects (PSO) valdymo ir PM4 komandų buferių konstravimo.

2. **Vulkan ICD ir Šeiderių Kompiliavimas**:
   - **Būklė**: [bc250_vulkan_icd.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/vulkan/bc250_vulkan_icd.c) turi Vulkan 1.0–1.2 karkasą su ACO jungtimi (`bc250_aco_wrapper.c`).
   - **Trūkumas**: Trūksta galutinio SPIR-V vertimo į GFX10.1.3 (RDNA2 Cyan Skillfish) ISA instrukcijas ir komandų buferių pateikimo (submit) per IOCTL vykdymui.

### C. Programinė Įranga, Testavimas ir Projekto Higiena

1. **Vieninga Automatinė Testavimo Sistema (Test Runner)**:
   - **Būklė**: `test-tools/` kataloge yra virš 100 atskirų `.c` testų.
   - **Trūkumas**: Nėra vieningo testų paleidiklio (pvz., `run-all-tests.ps1`), kuris atliktų regresinį patikrinimą ir išvestų `PASS/FAIL` ataskaitą.

2. **Vieno Mygtuko Windows Įdiegimo Programėlė (Installer)**:
   - **Būklė**: Įdiegiama rankiniu būdu per Device Manager arba [reinstall-gpu-driver.bat](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/reinstall-gpu-driver.bat).
   - **Trūkumas**: Trūksta patogios Windows Setup GUI programos, kuri patikrintų `testsigning` režimą, įdiegtų sertifikatą, sukelto failus ir įdiegtų INF.

3. **Projekto Katalogo Valymas (Codebase Hygiene)**:
   - **Būklė**: Root kataloge yra likusių `.obj`, `.log` ir senų atsarginių failų (`amdbc250_dream_kmd.c.bak`, `amdbc250_umd_v46.c.bak`).
   - **Trūkumas**: Trūksta `.obj` failų izoliavimo `obj/` kataloge, nenaudojamų `.bak` failų pašalinimo ir švaraus `.gitignore`.

---

## 3. Prioritetiniai Kiti Žingsniai

1. **SMU Laikrodžių Handshake Implementacija**: Užbaigti [amdbc250_dream_power.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_power.c) SMU žinučių siuntimą (`cyan_skillfish2_smc.bin`), kad GFX/Compute blokai gautų taktinį dažnį.
2. **VBIOS Nuskaitymas iš ACPI VFCT**: Integruoti ATOM BIOS parsintuvą iš ACPI VFCT lentelės ([amdbc250_dream_vbios.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_vbios.c)).
3. **Automatinio Testų Runnerio Sukūrimas**: Sujungti svarbiausius `test-tools/` testus į vieną automatinį regresinį skriptą.
4. **Projekto Švara**: Pašalinti `.bak` failus ir sutvarkyti kodo struktūrą.

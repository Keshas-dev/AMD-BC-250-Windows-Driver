# AMD BC-250 Windows Driver Project

## Who We Are

AMD BC-250 Windows driver project by Keshas. Goal: fully working GPU driver for AMD BC-250 (Cyan Skillfish) on Windows.

**Everyone welcome!** GPU drivers, WDDM, Vulkan experience — or just want to help.

## Hardware

- **SoC:** AMD BC-250 (Cyan Skillfish) — 24 CU RDNA2, 16GB GDDR6 shared memory
- **GPU ID:** 0x9FFF9700, **BIOS:** P4.00G
- **Memory:** CPU and GPU share GDDR6 (UMA) — VRAM at 0xC0000000
- **GPU BAR5:** 0xFE800000 (512KB MMIO register space)
- **PSP BAR0:** 0xFD600000 (256KB)
- **GC_BASE:** 0x1260 (BC-250 uses shifted register offsets vs Navi10)

---

## Achievements (What We Accomplished)

### Working Infrastructure
- ✅ **GPU driver loads**, all 30+ IOCTL handlers operational
- ✅ **BAR5 MMIO mapping** — `DreamV3WriteRegister`/`ReadRegister` via `WRITE_REGISTER_ULONG`
- ✅ **Build + sign pipeline** — `build.bat` with prebuild validation (IOCTL uniqueness, leak patterns)
- ✅ **PSP proxy driver bridge** — GPU driver supplies BAR5 access to PSP driver on Win11 26100
- ✅ **NBIO_MAP init** — safe init path (flag=1), avoids GPU alive test hang

### Firmware & Engine Discovery
- ✅ **CP firmware loads successfully** via MMIO IC_BASE DMA (IC_BASE offsetai pataisyti)
- ✅ **MEC firmware execution verified** — first bytes ucode corrupt → SCRATCH pasikeitė (engine tikrai veikia)
- ✅ **MEC2 firmware** turi realų ARM64 kodą (MEC1 tik NOP) — abu elgiasi identiškai
- ✅ **IC_BASE rašymo tvarka** — LO→HI→CNTL=0 (ne 0x100), polling 500ms

### Hardware Register Map (BC-250 specific)
- ✅ **GC_BASE=0x1260** — visi GC registrai offsetinti
- ✅ **GRBM_STATUS** (0x3260), GRBM_GFX_INDEX (0x34D0) — veikia
- ✅ **SCRATCH** (0x32D4) — writable, high nibble [31:28] HW-masked
- ✅ **KIQ_BASE_LO** (0xE060) writable; **KIQ_BASE_HI** read-only; **KIQ_SIZE** (0xE068) read-only=0
- ✅ **KIQ_WPTR** — tik 9 bitai (mask 0x1FF)
- ✅ **CP_HQD_*** (0xDAC0-0xDBFF) — visi NBIO blokuoti (rašymas tyliai nukrenta)
- ✅ **GCVM** — PT_BASE0 (0x0B408) writable; PT_BASE (0x0B608) HW-locked
- ✅ **TLB invalidacija** veikia (0x6C0C/0x6C10 protocol)
- ✅ **GPU_ID** = 0x9FFF9700, **GRBM_STATUS** = 0x00000000 (idle)

### Software PM4 Executor (Confirmed Working)
- ✅ **DreamV3SwPm4Process** — CPU verčia PM4 paketus į tiesioginius MMIO rašymus
- ✅ IT_WRITE_DATA (SCRATCH keičiasi), IT_NOP, IT_EVENT_WRITE_EOP, PM4_TYPE_0 — **patvirtinta HW**
- ✅ IT_SET_CONFIG_REG, IT_SET_CONTEXT_REG, IT_SET_SH_REG, IT_INDIRECT_BUFFER (su depth guard)
- ✅ **PATH 3 fallback** SEND_PM4 IOCTL kelyje — kai PSP KIQ ir GfxRing nepasiekiami
- ✅ Rekursijos gylio apsauga (max 32) — apsauga nuo stack overflow

### SMU v11.8
- ✅ **MP1_BASE**=0x16000, C2PMSG_66/82/90, THM_BASE=0x16600
- ✅ Protocol: clear C2PMSG_90 → arg → msg → poll
- ✅ SMU minimalus: jokių PowerUpGfx, EnableDpmFeature, SetFanSpeedPercent

### Bug Detection & Fixes
- ✅ **PM4 TYPE3 header encoding** — visi testai naudojo neteisingą `0xC0370003` (opcode=0, count=56 vietoj IT_WRITE_DATA)
- ✅ **IC_BASE offsetai** — buvo 0x7C10-0x7C18, teisingi 0x17390-0x17398
- ✅ **IC_BASE_CNTL** — rašė 0x100 (ENABLE) prieš nustatant base adresą; pataisyta į LO→HI→CNTL=0
- ✅ **UCODE_ADDR** — trūko polling (500ms timeout) prieš unhalt
- ✅ **SDMA init** — sukėlė BSOD 0x1a (MEMORY_MANAGEMENT); pataisyta su sanity check
- ✅ **KIQ deactivation** — prieš rašant KIQ registers, būtina deactivate (kitaip hang)
- ✅ **GRBM_GFX_INDEX** — būtina restore į broadcast (0xE0000000) po testų
- ✅ **17 bugs found** per code review (kritiniai: integer overflow, aligment, IOCTL collision, race conditions)

---

## Mistakes & Lessons Learned

### Critical Mistakes
1. **IC_BASE_CNTL=0x100 prieš nustatant base adresą** — jei bit 8 auto-started DMA iš 0 adreso, firmware niekada neįkraunamas. Turėtų būti: LO → HI → CNTL=0
2. **PM4 TYPE3 header encoding** — `(3<<30) | ((count-1)<<16) | (op<<8)` bet visi testai naudojo `0xC0370003` kuris yra IT_NOP count=56, ne IT_WRITE_DATA. Bug'as niekada nepasireiškė nes KIQ niekada neapdorojo ringo.
3. **CP_HQD writability claim** — anksčiau teigta kad 0xDAC0+ rašomi; iš tiesų NBIO blokuoti. Buvo suklaidinti aliased/stale readback reikšmių.
4. **RLC firmware loading** — bandyta krauti RLC firmware per 0x3A00 registrus kurie yra FREEZE ZONE (0x3400-0x8100) — BC-250 BIOS/SMU krauna RLC.
5. **KIQ_SIZE patch expectation** — manyta kad galima pakeisti MEC firmware kad KIQ_SIZE nebūtų 0; bet 0xE068 adresas firmware binary nerastas — check yra hardware lygyje.

### Wasted Effort
1. **GRBM_GFX_CNTL (0x2022)** — bandyta kad atrakint HQD; neveikia BC-250.
2. **comprehensive-pm4-test** — palieka KIQ active + ME_CNTL unhalted → system hang
3. **PSP driver KIQ path** — PSP KIQ ring turi tą pačią problemą (WPTR eina, RPTR=0)
4. **RLC init** — DreamV3InitRlc yra no-op; registrai freeze zone

### Technical Lessons
- **NBIO blokavimas** — 0xC000+ range yra NBIO protected; GC_BASE shifted aliases gali apeiti bet ne visiems registrams
- **WRITE_REGISTER_ULONG vs volatile*** — Win11 26100 volatile pointer write gali būti tyliai dropintas; WRITE_REGISTER_ULONG visada veikia
- **Firmware execution** įrodomas corruptinant ucode pirmus 8 baitus ir stebint SCRATCH
- **KIQ_WPTR** tik 9 bitai (ne 32) — HW apribojimas
- **SDMA init** gali sukelti BSOD jei registrai neteisingi; reikia sanity check
- **Code review prieš buildą** — sutaupo valandų debugging (rasti 17 bugs)

---

## Fundamental Blockers (Why Not 3D Ready)

| Blocker | Root Cause | Can We Fix? |
|---------|-----------|-------------|
| **KIQ_SIZE=0 read-only** | Hardware-level; adresas 0xE068 nėra firmware binary | ❌ Ne |
| **CP_HQD NBIO-blocked** | NBIO firewall blokuoja 0xDAC0-0xDBFF | ❌ Ne |
| **GCVM PT_BASE HW-locked** | Always reads 0; cannot configure page tables | ❌ Ne |
| **GFX_RING0_BASE_LO read-only** | BIOS nustato ring base; rašymas ignoruojamas | ❌ Ne |
| **KIQ_WPTR 9-bit limit** | Max ring 2048 bytes; HW apribojimas | ❌ Ne |
| **SOS firmware no ring protocol** (PSP side) | C2PMSG_64 bit 31 never sets; TOS nepalaiko GPCOM | ❌ Ne |

**Išvada:** Šis BC-250 variantas yra factory-locked GPU command execution. Visi hardware ring keliai užrakinti. **3D graphics with this specific hardware is not achievable.**

---

## What We Built Anyway (Software Workaround)

Kadangi HW ringai nepasiekiami, sukūrėme **Software PM4 executor** kuris:
- Priima standartinius PM4 paketus (IT_WRITE_DATA, IT_SET_CONFIG_REG, IT_INDIRECT_BUFFER ir t.t.)
- Išverčia juos į tiesioginius MMIO rašymus (CPU darbas, ne GPU)
- Leidžia testuoti bet kokį PM4 paketą be hardware ring rizikos

Tai leidžia **valdyti GPU registrus** ir suprasti hardware, bet **nepakeičia GPU shader execution**.

---

## Next Steps / Future Directions

1. **Complete BC-250 register map for other researchers** — visi offsetai, writability, blokeriai
2. **Fix remaining 15 bugs** from code review — integer overflow, alignment, race conditions
3. **Investigate alternative 40 CU unlock** per CC_GC_SHADER_ARRAY_CONFIG (0x3264) + SPI_PG_MASK (0x34FC)
4. **SDMA engine** — jei 0xE000 registrai gyvi, galima DMA be CP/KIQ
5. **Open-source documentation** — visos discovery dalintis su community
6. **Port to newer AMD GPU** — kur hardware neužrakintas (RDNA3+)

---

## Architecture

This is a **WDM control/IOCTL driver**, not a real WDDM miniport. `DxgkInitialize` is not exported on Windows 11 26100.

```
┌─────────────────────────────────────────────────┐
│              User Applications                    │
├─────────────────────────────────────────────────┤
│    gpu-kiq-test.exe / safe-test.exe / etc.       │
│    → DeviceIoControl → \\.\AMDBC250DreamV43      │
├─────────────────────────────────────────────────┤
│         atikmdag.sys (KMD — WDM)                  │
│         ├── DriverEntry                           │
│         ├── IRP_MJ_DEVICE_CONTROL (30+ handlers) │
│         ├── INIT_HARDWARE (MMIO map, Flags=1)    │
│         ├── READ_REG / WRITE_REG (BAR5 MMIO)    │
│         │   └── DreamV3WriteRegister/ReadRegister │
│         ├── GPU_KIQ_TEST — PM4 ring test          │
│         ├── PSP proxy (amdbc250_psp.c)            │
│         └── SMU v11.8 mailbox                    │
├─────────────────────────────────────────────────┤
│              AMD BC-250 GPU                        │
│              24 CU RDNA2, 16GB GDDR6              │
│              GC_BASE=0x1260                       │
└─────────────────────────────────────────────────┘
```

---

## How to Build

### Prerequisites
- Visual Studio 2022 (Professional) — auto-detected on D: or E: drive
- Windows WDK 10.0.26100.0
- Test signing: `bcdedit /set testsigning on` (Admin), Secure Boot OFF

### Build
```cmd
build.bat
```

### Install
1. `build.bat` → `output\atikmdag.sys`
2. Device Manager → AMD Radeon BC-250 → **Uninstall device** (check "Delete driver")
3. **Reboot**
4. Device Manager → Update Driver → Browse → `output\`
5. **Reboot**

### Test
```cmd
output\gcvm-pt-test.exe         # GCVM page table setup + KIQ test
output\gpu-kiq-test.exe         # PM4 ring execution test (uses wrong header)
output\safe-test.exe            # Safe read-only register test
output\iommu-gcvm-check.exe     # IOMMU + GCVM register scan
output\kiq-diag.exe             # Full KIQ diagnostic
test-tools\sw-pm4-test.exe      # Software PM4 executor test (confirmed working)
```

---

## File Structure

```
├── src/kmd/                        # Kernel-Mode Driver
│   ├── amdbc250_dream_kmd.c        # DriverEntry, IOCTL dispatch
│   ├── amdbc250_dream_hw_init.c    # GPU init, ring buffers, PSP
│   ├── amdbc250_dream_power.c      # Power/thermal management
│   ├── amdbc250_dream_vm.c         # GPUVM, GART, page tables
│   ├── amdbc250_psp.c              # PSP proxy driver interface
│   └── firmware_data.h             # Embedded PSP firmware
├── src/umd/                        # User-Mode Driver
│   └── amdbc250_umd_v46.c          # D3D9 DDI (45+ functions)
├── inc/                            # Shared headers
│   ├── amdbc250_dream_hw.h         # Hardware register definitions
│   └── amdbc250_ioctl.h            # IOCTL codes + structures
├── test-tools/                     # Diagnostic tools
│   ├── gpu-kiq-test.c              # PM4 ring execution test
│   ├── iommu-gcvm-check.c          # IOMMU/GCVM register scan
│   ├── kiq-diag.c                  # KIQ register diagnostic
│   └── kiq-unhalt.c                # ME_CNTL unhalt test
├── output/                         # Build output (signed drivers)
├── docs/                           # Technical documentation
├── build.bat                       # Build + sign driver
├── prebuild-check.ps1               # Pre-build validation
├── reinstall-both-drivers.bat       # Reinstall GPU + PSP drivers
└── .gitignore
```

---

## Key Documentation

| File | Description |
|------|-------------|
| [AGENTS.md](AGENTS.md) | Agent memory — hardware facts, current blockers |
| [docs/BC250-LINUX-IP-MAP.md](docs/BC250-LINUX-IP-MAP.md) | Linux-verified IP base addresses |
| [docs/REGISTER-MAP-BC250.md](docs/REGISTER-MAP-BC250.md) | Complete BC-250 register map |
| [docs/RING-INIT-STATUS.md](docs/RING-INIT-STATUS.md) | Ring init blockers and KIQ path |
| [docs/PSP-PROXY-BYPASS.md](docs/PSP-PROXY-BYPASS.md) | PSP proxy architecture |
| [docs/GCVM-ANALYSIS.md](docs/GCVM-ANALYSIS.md) | GCVM page table investigation |

---

## Related Projects

- **GPU Driver**: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver
- **PSP Driver**: https://github.com/Keshas-dev/AMD-BC-250-PSP-Windows-Driver

## License

Source code for educational purposes. Use at your own risk.
ACO compiler: MIT license (Mesa project).

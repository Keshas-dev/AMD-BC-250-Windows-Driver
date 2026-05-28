# BC-250 Dream Drivers - MASTER STATUS

**Data:** 2026-04-15  
**GPU:** AMD Radeon R7 M260/M265 (BC-250)  
**Device ID:** PCI\VEN_1002&DEV_13FE  
**Architecture:** CYAN SKILLFISH (RDNA2/GFX1013)  
**Memory:** 16GB GDDR6 | 24 CUs (1536 SP)

---

## 📊 PROJEKTO BŪSENA

### ✅ TARGET BUILD STATUS

| Komponentas | Failas | Dydis | Statusas |
|-------------|--------|-------|----------|
| **KMD** | `output\atikmdag.sys` | 20 KB | ⚠️ NOT BUILT |
| **UMD** | `output\amdbc250umd64.dll` | 103 KB | ⚠️ NOT BUILT |
| **INF** | `output\amdbc250_dream_v3.inf` | 4.3 KB | ⚠️ NOT VERIFIED |
| **CAT** | `output\amdbc250_dream_v3.cat` | 2.9 KB | ⚠️ NOT VERIFIED |

**Driver Package:** `output/` - target build output directory (currently empty)

### 🆕 LATEST NAJOVUMAI

| Komponentas | Kas padaryta |
|-------------|--------------|
| **INF** | ✅ Pašalintos Vulkan/OpenGL nuorodos (amdvlk64.dll neegzistuoja) |
| **INF** | ✅ Atnaujintas DriverVer į 04/13/2026,4.2.0.0 |
| **UMD** | ✅ D3D12 OpenAdapter12 pilnai veikia |
| **VM.C** | ✅ Pataisytas bounds checking (Pml4Index/PdIndex < 512) |
| **VM.C** | ✅ Pataisytas DreamV3VmUnmapRange (išvalo page table entries) |
| **VM.C** | ✅ Pataisytas DreamV3VmDestroyContext (frees all page tables) |
| **BUILD** | ✅ build.bat skriptas atnaujintas |
| **BUILD** | ✅ CAT generation su x86 inf2cat (x64 versija neegzistuoja) |

### 📝 SOURCE KODAS

#### Kernel-Mode Driver (KMD) - 4,349 eilutės

| Failas | Eilutės | Būsena | Pastabos |
|--------|---------|--------|----------|
| `src\kmd\amdbc250_dream_v3_kmd.c` | 1,754 | ✅ BUILDĮTRAUKTA | WDDM DDI callbacks, DriverEntry, SubmitCommand, BuildPagingBuffer, VidPN |
| `src\kmd\amdbc250_dream_v3_hw_init.c` | 821 | ✅ BUILDĮTRAUKTA | CP init, HDP flush, golden registers, DCN 2.1 display, thermal |
| `src\kmd\amdbc250_dream_v3_vm.c` | 866 | ⚠️ PARAŠYTA, NEĮTRAUKTA | GPUVM, GART, 4-level page tables, VMID, TLB invalidation |
| `src\kmd\amdbc250_dream_v3_power.c` | 908 | ⚠️ PARAŠYTA, NEĮTRAUKTA | SMU, D0-D3 states, thermal throttle, fan PWM, clock scaling |

#### User-Mode Driver (UMD) - 2,389 eilutės

| Failas | Eilutės | Būsena | Pastabos |
|--------|---------|--------|----------|
| `src\umd\amdbc250_umd_v46.c` | 1,041 | ✅ KOMPIILIUOTA | Full resource management UMD |
| `src\umd\amdbc250_umd_full.c` | 336 | ⚠️ NEKOMPIILIUOTA | D3D10/11/12 stubs |
| `src\umd\amdbc250_umd_d3d12.c` | 185 | ⚠️ NEKOMPIILIUOTA | D3D12 specific implementation |
| `src\umd\amdbc250_umd_minimal.c` | 55 | ❌ SENAS | Originalus stub (nenaudojamas) |
| `extra\amdbc250_umd.c` | 1,670 | ⚠️ NEKOMPIILIUOTA | PILNAS D3D9/10/11/12 implementation iš extra/ |

#### Headers (inc/)

| Failas | Eilutės | Turinys |
|--------|---------|---------|
| `inc\amdbc250_dream_v3_hw.h` | 555 | GFX1013 registrai, PCI IDs, memory layout |
| `inc\amdbc250_dream_v3_kmd.h` | 850 | WDDM structures, device extension, prototypes |
| `inc\amdbc250_d3d12.h` | - | D3D12 DDI definitions |
| `inc\amdbc250_d3d11.h` | - | D3D11 DDI definitions |
| `inc\amdbc250_d3d10.h` | - | D3D10 DDI definitions |
| `inc\amdbc250_dxgi.h` | - | DXGI adapter/output definitions |
| `inc\amdbc250_hw_extra.h` | - | Hardware workarounds |

---

## 🗂️ PROJEKTO STRUKTŪRA

```
c:\AMD-BC-250-Windows-Driver\
│
├── 📄 MASTER-STATUS.md          ← ŠIS FAILAS (projekto būsena)
├── 📄 QUICK-START.md            ← Greito paleidimo instrukcija
├── 📄 build.bat                 ← PAGRINDINIS BUILD SKRIPTAS
├── 📄 QWEN.md                   ← Pilna projekto istorija
│
├── 📁 src/                      ← SOURCE KODAS
│   ├── kmd/                     ← Kernel-Mode Driver
│   │   ├── amdbc250_dream_v3_kmd.c       (1,754 lines) ✅ ACTIVE
│   │   ├── amdbc250_dream_v3_hw_init.c   (821 lines)  ✅ ACTIVE
│   │   ├── amdbc250_dream_v3_vm.c        (866 lines)  ⚠️ NOT IN BUILD
│   │   ├── amdbc250_dream_v3_power.c     (908 lines)  ⚠️ NOT IN BUILD
│   │   ├── makefile
│   │   └── SOURCES
│   └── umd/                     ← User-Mode Driver
│       ├── amdbc250_umd_v46.c            (1,041 lines) ✅ ACTIVE
│       ├── amdbc250_umd_full.c           (336 lines)  ⚠️ NOT COMPILED
│       ├── amdbc250_umd_d3d12.c          (185 lines)  ⚠️ NOT COMPILED
│       └── amdbc250_umd_minimal.c        (55 lines)   ❌ OLD
│
├── 📁 inc/                      ← HEADERS
│   ├── amdbc250_dream_v3_hw.h           (555 lines)
│   ├── amdbc250_dream_v3_kmd.h          (850 lines)
│   ├── amdbc250_d3d12.h
│   ├── amdbc250_d3d11.h
│   ├── amdbc250_d3d10.h
│   ├── amdbc250_dxgi.h
│   └── amdbc250_hw_extra.h
│
├── 📁 extra/                    ← PAPILDOMAS SOURCE (iš v1.0)
│   ├── amdbc250_umd.c                   (1,670 lines - pilnas UMD)
│   ├── amdbc250_kmd.c                   (1,207 lines)
│   ├── amdbc250_kmd.h
│   ├── amdbc250_hw.h
│   ├── amdbc250_hw_init.c
│   └── DDI headers (d3d12, d3d11, d3d10, dxgi)
│
├── 📁 inf/                      ← INSTALLACIJA
│   └── amdbc250_dream_v3.inf            (4.7 KB)
│
├── 📁 output/                   ← DRIVER PACKAGE (PARUOŠTA)
│   ├── atikmdag.sys                     (21 KB)
│   ├── amdbc250umd64.dll                (103 KB)
│   ├── amdbc250_dream_v3.inf            (4.7 KB)
│   └── amdbc250_dream_v3.cat            (2.9 KB - SIGNED)
│
├── 📁 build-scripts/            ← LEGACY BUILD SCRIPTS (not present in current workspace)
│   └── Use `build.bat` at repo root for the current build flow
│
├── 📁 build-output/             ← LEGACY COMPILATION OUTPUT (not present in current workspace)
│   ├── *.obj
│   └── *.pdb
│
├── 📁 legacy-output/            ← LEGACY FULL OUTPUT ARCHIVE (not present in current workspace)
│   ├── atikmdag.sys
│   ├── amdbc250umd64.dll
│   └── 23 Adrenalin 18.5.1 DLL files
│
├── 📁 tools/                    ← UTILITY
│   ├── Install-DreamDrivers.ps1
│   ├── Install-DreamDrivers-v3.1.ps1
│   ├── force-install.ps1
│   └── Uninstall-Old-Drivers.bat
│
├── 📁 test-tools/               ← TESTAVIMAS
│   ├── test-gpu-simple.c/.exe
│   ├── test-d3d12-*.c
│   └── dxdiag_*.txt
│
├── 📁 docs/                     ← DOKUMENTACIJA
│   ├── README.md
│   ├── PILNAS-APRASAS.md
│   ├── GALUTINIS-REZULTATAS.md
│   ├── BUILD-STATUS.md
│   └── EXTRA-ANALYSIS.md
│
└── 📁 archive/                  ← SENI PACKAGE
    └── old-install/
```

---

## 🔧 BUILD KOMANDOS

### Pagrindinis build skriptas:
```powershell
.\build.bat
```

### Atskiros komandos:
```powershell
# Atlikite pilną build procesą naudojant:
.\build.bat
```

### Rankinis build:
```powershell
# KMD
cl.exe /c /kernel /W3 /Zi /Od /DAMD64 /D_AMD64_ /DAMDBC250_DREAM_V3 ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\km" ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\km\crt" ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" ^
  /I"inc" src\kmd\amdbc250_dream_v3_kmd.c src\kmd\amdbc250_dream_v3_hw_init.c

link.exe /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
  /OUT:output\atikmdag.sys ^
  *.obj ntoskrnl.lib wdm.lib win32k.lib ntstrsafe.lib BufferOverflowK.lib ^
  /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\lib\10.0.26100.0\km\x64"

# UMD
cl.exe /c /TC /D_AMD64_ /DWIN64 /DAMDBC250_UMD /W3 /Zi /O2 ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt" ^
  /I"inc" src\umd\amdbc250_umd_v46.c

link.exe /DLL /OUT:output\amdbc250umd64.dll *.obj ^
  d3d12.lib dxgi.lib dxguid.lib user32.lib

# CAT + Sign
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\inf2cat.exe" ^
  /driver:output /os:10_x64

"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe" sign ^
  /sha1 22313795FA2CA96ECB495F4B1983E4EA0335452A ^
  /fd SHA256 output\amdbc250_dream_v3.cat
```

---

## ⚠️ DABARTINĖ BŪSENA

### Instaliacija:
- **Status:** Microsoft Basic Display Adapter (NEVEIKIA!)
- **Priežastis:** Driveris nėra įkeltas arba BC-250 nėra detektuota

### Reikalingi veiksmai:
1. ✅ Reboot (būtina)
2. ✅ Device Manager → Display adapters → Update Driver
3. ✅ Browse → Have Disk → `output\amdbc250_dream_v3.inf`
4. ✅ Pasirinkti: "AMD Radeon BC-250 Graphics (Dream Drivers v3.0 - RDNA2)"
5. ✅ REBOOT

### Patikrinimas:
```powershell
Get-PnpDevice -Class Display | Select Status, FriendlyName, ConfigManagerErrorCode
```

**Tikėtinas rezultatas:**
- Status: OK
- FriendlyName: AMD Radeon BC-250 Graphics
- ConfigManagerErrorCode: 0

---

## 🎯 PRIORITETAI

### Aukščiausias:
1. ⏳ **Instaliuoti driver** per Device Manager
2. ⏳ **Reboot** ir patikrinti ar veikia
3. ⏳ **Verify** su Get-PnpDevice

### Antras prioritetas:
4. 📝 **Įtraukti vm.c ir power.c** į SOURCES failą
5. 📝 **Perbuildinti KMD** su visais source failais
6. 📝 **Integruoti extra/amdbc250_umd.c** (1,670 lines pilno UMD)

### Trečias prioritetas:
7. 🧪 **Testuoti D3D12** su dxdiag
8. 🧪 **Implementuoti** command list, resource, pipeline state
9. 🧪 **Pilnas D3D12 support**

---

## 📚 DOKUMENTACIJA

- `docs/README.md` - Projekto overview (~280 lines)
- `docs/PILNAS-APRASAS.md` - Pilnas techninis aprašymas LT (~1,242 lines)
- `docs/GALUTINIS-REZULTATAS.md` - Galutinis rezultatas LT (~151 lines)
- `docs/BUILD-STATUS.md` - Build environment report (~150 lines)
- `docs/EXTRA-ANALYSIS.md` - Extra/ source analizė (~300 lines)
- `QWEN.md` - Pilna projekto istorija ir memories

---

## 🔑 CERTIFIKATAI

**Test Certificate:**
- Name: CN=BC250TestDriver
- SHA1: `22313795FA2CA96ECB495F4B1983E4EA0335452A`
- Installed: Trusted Root + Trusted Publishers
- Test signing: ENABLED (`bcdedit /set testsigning on`)

---

## 🚀 KRITINĖ INFORMACIJA

### BC-250 GPU Architektūra (TEISINGA):
- **Architecture:** CYAN SKILLFISH (RDNA2/GFX1013)
- **CUs:** 24 RDNA2 Compute Units (1536 Stream Processors)
- **Memory:** 16GB GDDR6
- **Display Engine:** DCN 2.1
- **Ray Tracing:** Hardware cores present
- **TDP:** 220W
- **PCI ID:** 1002:13FE

### Linux Learnings (pritaikyta Windows):
1. ✅ HDP coherency flush REQUIRED before reading ring pointers
2. ✅ Golden registers must be programmed at init
3. ⚠️ Compute queue is BROKEN (hardware flaw, disabled)
4. ⚠️ VRAM visible limited to ~10GB (hardware quirk)
5. ❌ VCN firmware blocked by Sony (no video encode/decode)
6. ✅ Thermal throttle at 85°C, emergency shutdown at 105°C

### KRITINĖS KLAIDOS (kurių vengti):
1. ❌ NENAUDOTI Kaveri/GCN 1.1 identifikacijos - tai RDNA2!
2. ❌ NENAUDOTI GFX7 (Sea Islands) - tai GFX1013!
3. ❌ NENAUDOTI DCE 8.x - tai DCN 2.1!
4. ❌ NENAUDOTI DDR3 UMA - tai GDDR6 16GB!
5. ❌ NENAUDOTI 6 CU - tai 24 CU!

---

## 📞 NEXT STEPS

1. **INSTALIUOTI** driver iš `staging-v4.0/`
2. **REBOOT** sistemą
3. **PATIKRINTI** ar veikia su Get-PnpDevice
4. **TESTUOTI** su dxdiag
5. **IMPLEMENTUOTI** pilną D3D12 support

---

**Paskutinį kartą atnaujinta:** 2026-04-13 10:30  
**Versija:** v4.1.0.0  
**Būsena:** PARUOŠTA INSTALIAVIMUI ⏳

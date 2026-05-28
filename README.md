# AMD BC-250 Windows Dream Drivers

**Projekto tikslas:** Sukurti veikiančius Windows GPU draiverius AMD BC-250 (Cyan Skillfish / RDNA2/GFX1013) plokštei.

**Dabartinė versija:** v4.2 (2026-04-15)  
**Būsena:** KMD kompiliuojasi ir instaliuojasi, UMD stub'ai dalinai įgyvendinti.

## 📁 Projekto struktūra

```
AMD-BC-250-Windows-Driver/
├── QUICK-START.md          ← Greito paleidimo instrukcija
├── MASTER-STATUS.md        ← Dabartinė būsena ir struktūra
├── QWEN.md                 ← Projekto istorija ir atmintys
├── docs/                   ← Techninė dokumentacija
│   ├── README.md           ← Projekto overview (EN)
│   ├── PILNAS-APRASAS.md   ← Išsamus techninis aprašymas (LT)
│   ├── BUILD-STATUS.md     ← Build aplinkos ataskaita
│   └── D3D12-UMD-RESEARCH.md
├── src/                    ← Šaltinis kodas
│   ├── kmd/                ← Kernel-Mode Driver (aktyvus)
│   └── umd/                ← User-Mode Driver (stub'ai)
├── inc/                    ← Header failai
├── inf/                    ← Instaliacijos failai
├── tools/                  ← Įrankiai ir skriptai
├── test-tools/             ← Testavimo įrankiai
└── output/                 ← Build output (jei nėra, buildinti iš src/)
```

## 🚀 Greitas pradžia

1. **Reikalavimai:**
   - Windows 11 (64-bit)
   - Visual Studio 2022 su WDK
   - Test signing įjungtas: `bcdedit /set testsigning on`

2. **Build ir instaliacija:**
   ```powershell
   .\build.bat
   # Pasirinkti 1 (Full Build)
   ```
   Arba naudoti prebuilt iš `output/` aplanko.

3. **Instaliuoti:**
   - Device Manager → Update Driver → Browse → `output\amdbc250_dream_v3.inf`
   - Pasirinkti: "AMD Radeon BC-250 Graphics (Dream Drivers v4.2 - RDNA2)"

4. **Patikrinti:**
   ```powershell
   Get-PnpDevice -Class Display | Select Status, FriendlyName
   ```

## 📊 Būsena

| Komponentas | Statusas | Pastabos |
|-------------|----------|----------|
| **KMD** | ✅ Kompiliuojasi | WDDM DDI callbacks, HW init |
| **UMD** | ⚠️ Stub'ai | D3D12 baziniai, ne pilnas |
| **Build** | ✅ Veikia | MSVC + WDK |
| **Instaliacija** | ✅ Veikia | Device Manager |
| **D3D12** | ⚠️ Bazinis | OpenAdapter12 veikia |
| **Display** | ✅ DCN 2.1 | Iš Linux amdgpu |
| **Ray Tracing** | ❌ Neįgyvendinta | Aparatinė įranga turi RT cores |

## 📚 Dokumentacija

- **[QUICK-START.md](QUICK-START.md)** - Greitos pradžios gidas
- **[MASTER-STATUS.md](MASTER-STATUS.md)** - Išsami būsena ir struktūra
- **[docs/README.md](docs/README.md)** - Projekto istorija ir specifikacijos
- **[docs/PILNAS-APRASAS.md](docs/PILNAS-APRASAS.md)** - Techninis aprašymas (lietuviškai)
- **[docs/BUILD-STATUS.md](docs/BUILD-STATUS.md)** - Build aplinkos problema ir sprendimai
- **[QWEN.md](QWEN.md)** - Projekto atmintys ir istorija

## 🐧 Linux palyginimas

Šis projektas bando atkartoti Linux `amdgpu` driver funkcionalumą Windows aplinkoje:

| Funkcija | Linux (amdgpu) | Windows (Dream Drivers) |
|----------|----------------|-------------------------|
| Vulkan | ✅ RADV | ⚠️ AMDVLK reikalingas |
| OpenGL | ✅ radeonsi | ❌ Neįgyvendinta |
| D3D12 | ✅ DXVK | ⚠️ Bazinis stub |
| Compute | ❌ Broken HW | ✅ Išjungta (quirk) |
| Video | ❌ Blocked | ❌ Blocked |
| Performance | ~RX 6600 | TBD |

## ⚠️ Žinomos problemos

- **Hardware limitations:** Broken compute queue, VCN firmware blocked, ~10GB VRAM limit
- **UMD:** Tik baziniai D3D12 stub'ai, nėra pilno rendering
- **Build:** Reikia WDK aplinkos
- **Testing:** Ribotas testavimas dėl aparatinės įrangos prieinamumo

## 🤝 Kontributoriai

Šis projektas yra bendruomeninis, skirtas AMD BC-250 savininkams, kurie nori Windows GPU palaikymo.

## 📄 Licencija

Šaltinis kodas yra atviras, bet skirtas mokymosi tikslams. Naudokite savo rizika.

---

*Atnaujinta: 2026-04-15*</content>
<filePath>file:///c:/AMD-BC-250-Windows-Driver/README.md
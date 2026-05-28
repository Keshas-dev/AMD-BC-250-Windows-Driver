# 🚀 BC-250 Dream Drivers - QUICK START

**Data:** 2026-04-15  
**Versija:** v4.2.0.0  
**GPU:** AMD Radeon BC-250 (RDNA2/GFX1013)

---

## 📋 REIKALAVIMAI

- ✅ Windows 11 (64-bit)
- ✅ Visual Studio 2022 (Community)
- ✅ Windows Driver Kit (WDK) 10.0.26100.0
- ✅ Test signing enabled: `bcdedit /set testsigning on`
- ✅ Certificate installed: CN=BC250TestDriver

---

## 🎯 GREITAS PRADŽIA (3 žingsniai)

### 1️⃣ BUILD (jei reikia perkompiliuoti)

```powershell
# Eiti į projektą:
cd c:\AMD-BC-250-Windows-Driver

# Paleisti build:
.\build.bat

# Pasirinkti: 1 (Full Build)
```

**Arba** jei driver package jau yra `output/`, eiti tiesiai į žingsnį 2.

### 2️⃣ INSTALIUOTI

**Metodas A - PowerShell (rekomenduojama):**
```powershell
cd c:\AMD-BC-250-Windows-Driver
.\tools\Install-DreamDrivers-v3.1.ps1
```

**Metodas B - Device Manager (rankinis):**
1. Atidaryti **Device Manager**
2. **Display adapters** → dešinys klavišas → **Update Driver**
3. **Browse my computer for drivers**
4. **Let me pick from a list...**
5. **Have Disk...**
6. **Browse** → `c:\AMD-BC-250-Windows-Driver\output\amdbc250_dream_v3.inf`
7. Pasirinkti: **"AMD Radeon BC-250 Graphics (Dream Drivers v4.2 - RDNA2)"**
8. **Next** → **Install**

### 3️⃣ REBOOT ✅

**RESTARTUOKITE SISTEMĄ!** (BŪTINA)

---

## ✅ PATIKRINIMAS PO REBOOT

### 1. Patikrinti ar driveris veikia:

```powershell
Get-PnpDevice -Class Display | Select Status, FriendlyName, ConfigManagerErrorCode
```

**Tikėtinas rezultatas:**
```
Status  FriendlyName              ConfigManagerErrorCode
------  ------------              ----------------------
OK      AMD Radeon BC-250 Graphics                     0
```

### 2. Patikrinti DxDiag:

```powershell
dxdiag /t dxdiag_check.txt
type dxdiag_check.txt
```

**Arba** rankiniu būdu:
- Win+R → `dxdiag` → **Display** tab
- **Card name:** AMD Radeon BC-250 Graphics
- **Manufacturer:** AMD
- **DDI Version:** 12

### 3. Patikrinti Event Log:

```powershell
Get-WinEvent -FilterHashtable @{LogName='System'; ID=7045} | 
  Where-Object {$_.Message -like "*atikmdag*"} | 
  Select -First 1
```

---

## ⚠️ JEI NEVEIKIA

### Problema: "Microsoft Basic Display Adapter"

**Sprendimas:**
1. Device Manager → Display adapters
2. Dešinys klavišas → **Uninstall device**
3. Pažymėti: **"Delete the driver software for this device"**
4. **Action** → **Scan for hardware changes**
5. Pakartoti instaliaciją iš `output/`

### Problema: "Code 31" arba "Code 43"

**Sprendimas:**
1. Atidaryti Event Viewer → Windows Logs → System
2. Ieškoti klaidų susijusių su **atikmdag** arba **amdbc250**
3. Patikrinti ar test signing įjungtas:
   ```powershell
   bcdedit | findstr testsigning
   ```
4. Jei nėra `testsigning Yes`, įjungti:
   ```powershell
   bcdedit /set testsigning on
   ```
5. Reboot

### Problema: "Driver signing error"

**Sprendimas:**
```powershell
# Reinstall the test certificate (use the current certificate path)
certutil -addstore Root <path-to-BC250Test.cer>
certutil -addstore TrustedPublisher <path-to-BC250Test.cer>

# Resign the catalog if needed using current repo layout
# Example: signtool.exe sign /sha1 <thumbprint> /fd SHA256 output\amdbc250_dream_v3.cat
```

---

## 🔧 BUILD IŠ NAUJO

### Tik KMD / UMD / Package:
```powershell
.\build.bat
```

# If you need a more specific build flow, add dedicated scripts or update build.bat accordingly.

### Pilnas build:
```powershell
.\build.bat
# Pasirinkti: 1
```

---

## 📁 FAILŲ STRUKTŪRA

```
c:\AMD-BC-250-Windows-Driver\
├── output\                 ← DRIVER PACKAGE (instaliuoti iš čia)
│   ├── atikmdag.sys        (21 KB - KMD)
│   ├── amdbc250umd64.dll   (103 KB - UMD)
│   ├── amdbc250_dream_v3.inf (4.7 KB)
│   └── amdbc250_dream_v3.cat (2.9 KB - SIGNED)
│
├── build.bat               ← Pagrindinis build skriptas
├── src\                    ← Source kodas
├── inc\                    ← Headers
├── tools\                  ← Instaliacijos skriptai
└── docs\                   ← Dokumentacija
```

---

## 📊 PROJEKTO BŪSENA

| Komponentas | Status |
|-------------|--------|
| KMD Source | ✅ 4,349 lines (2,575 active) |
| UMD Source | ✅ 2,389 lines (203 active) |
| Build System | ✅ VEIKIA |
| Driver Package | ✅ PARUOŠTA |
| Certificate | ✅ SIGNED |
| Instaliacija | ⏳ LAUKIA |

---

## 📚 DOKUMENTACIJA

- `MASTER-STATUS.md` - Pilna projekto būsena
- `docs/README.md` - Projekto overview
- `docs/PILNAS-APRASAS.md` - Techninis aprašymas (LT)
- `QWEN.md` - Projekto istorija

---

## 🎯 NEXT STEPS (po sėkmingos instaliacijos)

1. ✅ Test D3D12: `dxdiag` → Display tab → DDI Version 12
2. ✅ Test GPU: `.\test-tools\test-gpu-simple.exe`
3. ⏳ Implement full D3D12: command list, resources, pipelines
4. ⏳ Integrate vm.c + power.c (1,774 additional lines)
5. ⏳ Integrate full UMD from extra/ (1,670 lines)

---

## 🆘 PAGALBA

Jei kyla problemų:
1. Patikrinti `MASTER-STATUS.md`
2. Patikrinti `docs/` katalogą
3. Peržiūrėti Event Viewer logs
4. Patikrinti ar test signing įjungtas

---

**Paskutinį kartą atnaujinta:** 2026-04-13  
**Versija:** v4.1.0.0  
**Status:** READY TO INSTALL ⏳

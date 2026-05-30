# AMD BC-250 Windows Driver - Status Report

**Data:** 2026-05-30 (vakarinė sesija)
**Versija:** v4.5.0
**Projektas:** Windows GPU driver for AMD BC-250 (Cyan Skillfish / RDNA2 / GFX1013)

---

## 🏆 Šio vakaro pasiekimai

| Komponentas | Statusas |
|-------------|----------|
| Vulkan ICD veikia su oficialiu Vulkan loaderiu (vulkaninfo) | ✅ |
| 13/13 Vulkan testų PASS | ✅ |
| IOCTL kanalas veikia (13/15 testų) | ✅ |
| g_PciDevExt inicializuotas DriverEntry | ✅ |
| IB packet support grąžintas | ✅ |
| EOP fence formatas pataisytas (nebe crash) | ✅ |

---

## ✅ Komponentų statusas

### KMD (Kernel-Mode Driver) — `atikmdag.sys`

| # | Komponentas | Statusas |
|---|-------------|----------|
| 1 | IOCTL dispatch veikia | ✅ Pataisyta |
| 2 | g_PciDevExt inicializacija | ✅ Pataisyta |
| 3 | IB packet + EOP fence | ✅ Veikia |
| 4 | Dummy data IOCTL (GetCaps, GetVramInfo) | ✅ Veikia |
| 5 | AllocVidMem | ❌ BSOD (MmAllocateContiguousMemory) |
| 6 | Display flip | ⏳ Laukia |
| 7 | Hardware init (MMIO, ring, fence) | ⏳ Laukia |

### Vulkan ICD — `amdbc250vulkan.dll`

| # | Komponentas | Statusas |
|---|-------------|----------|
| 1 | vulkaninfo.exe praeina | ✅ Nauja! |
| 2 | 13/13 testų PASS | ✅ |
| 3 | QueueSubmit su IB | ✅ Veikia |
| 4 | vk_icdNegotiateLoaderICDInterfaceVersion | ✅ Pridėta |
| 5 | 80+ Vulkan stubs | ✅ Pridėtos |

### Build & Test

| # | Komponentas | Statusas |
|---|-------------|----------|
| 1 | build.bat | ✅ Veikia |
| 2 | test-gpu-ioctls.exe | ✅ 13/15 PASS |
| 3 | test-vulkan-icd.exe | ✅ 13/13 PASS |
| 4 | vulkaninfo.exe | ✅ PRAEINA |
| 5 | test-render.exe | ❌ AllocVidMem BSOD |

---

## 🔑 Kodėl kiti projektai failino

| Projektas | Kelias | Rezultatas |
|-----------|--------|------------|
| ps5-win-driver | WDDM (DxgkInitialize) | ❌ Code 43/39 |
| AMD-BC-250-Windows-Driver-2 | WDDM + AMD Adrenalin | ❌ Code 43 |
| ZEROAESQUERDA | WDDM DDI callbacks | ❌ Code 43 |
| **Mūsų projektas** | **Vulkan ICD + IOCTL** | ✅ **13/13 + vulkaninfo** |

---

## ⏳ Rytojaus užduotys

| # | Užduotis | Svarba |
|---|----------|--------|
| 1 | AllocVidMem BSOD priežastis (MmAllocateContiguousMemory) | 🔴 |
| 2 | Display flip (HUBPREQ registrų programavimas) | 🔴 |
| 3 | Hardware init DriverEntry (MMIO, ring, fence) | 🔴 |
| 4 | Realus triangle rendering | 🟡 |
| 5 | ACO shader compilation | 🟡 |

---

## 📊 Failų statistika

| Komponentas | Failai | Dydis |
|-------------|--------|-------|
| KMD source | 4 .c + 4 .h + 2 .def | ~230 KB |
| UMD source | 1 .c + 1 .def | ~55 KB |
| Vulkan ICD | 2 .c + 2 .h + 1 .def | ~65 KB |
| Build output | 10 failai | ~350 KB |
| Test tools | 5 failai | ~500 KB |
| **Iš viso** | **~35 failų** | **~1.2 MB** |

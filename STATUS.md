# AMD BC-250 Windows Driver - Status Report

**Data:** 2026-05-29
**Versija:** v4.3.1.0
**Projektas:** Windows GPU driver for AMD BC-250 (Cyan Skillfish / RDNA2 / GFX1013)

---

## ✅ Ką padarėme (30+ komponentų)

### KMD (Kernel-Mode Driver) — `atikmdag.sys` (40 KB)

| # | Komponentas | Failas | Statusas |
|---|-------------|--------|----------|
| 1 | WDDM 2.x DDI callbacks | `kmd.c` | ✅ Pilnas |
| 2 | Hardware init (GFX10, DCN 2.1) | `hw_init.c` | ✅ Pilnas |
| 3 | IOCTL dispatch (24 handler) | `kmd.c` | ✅ Pilnas |
| 4 | Ring buffers + interrupts | `kmd.c` | ✅ Pilnas |
| 5 | SDMA copy engine | `kmd.c` | ✅ Pilnas |
| 6 | TDR reset (6 žingsnių) | `kmd.c` | ✅ Pilnas |
| 7 | EDID parsing | `kmd.c` | ✅ Pilnas |
| 8 | Hotplug detection | `kmd.c` | ✅ Pilnas |
| 9 | VGA fallback (640x480) | `hw_init.c` | ✅ Pilnas |
| 10 | 40 CU unlock | `kmd.c` | ✅ Pilnas |
| 11 | Power/thermal management | `power.c` | ✅ Pilnas |
| 12 | GPU virtual memory | `vm.c` | ✅ Pilnas |
| 13 | PM4 Command Queue (fence event) | `kmd.c` | ✅ Pilnas |
| 14 | **SubmitCommands IOCTL: IB packet** | `kmd.c` | ✅ **TAISYTA** |

### UMD (User-Mode Driver) — `amdbc250umd64.dll` (108 KB)

| # | Komponentas | Statusas |
|---|-------------|----------|
| 15 | D3D9 DDI (45+ funkcijų) | ✅ Pilnas |
| 16 | **D3D9 GetCaps (nebe zero)** | ✅ **TAISYTA** |
| 17 | **OpenAdapter export (pfnGetCaps)** | ✅ **TAISYTA** |
| 18 | DrawPrimitive + PM4 packets | ✅ Pilnas |
| 19 | DMA command buffer alloc | ✅ Pilnas |
| 20 | Present + display flip | ✅ Pilnas |
| 21 | **Flush: GPU VA, ne CPU adresas** | ✅ **TAISYTA** |
| 22 | **CreateResource/Lock: teisingi IOCTL** | ✅ **TAISYTA** |

### Vulkan ICD — `amdbc250vulkan.dll` (133 KB)

| # | Komponentas | Statusas |
|---|-------------|----------|
| 23 | Vulkan 1.4 ICD | ✅ Pilnas |
| 24 | ACO shader compiler | ✅ SPIR-V → GFX10 ISA |
| 25 | QueueSubmit (KMD IOCTL) | ✅ Pilnas |
| 26 | QueuePresentKHR (display flip) | ✅ Pilnas |

### Build & Test

| # | Komponentas | Statusas |
|---|-------------|----------|
| 27 | Auto build (VS2022 + WDK) | ✅ build.bat |
| 28 | Auto signing (catalog + cert) | ✅ Automatinis |
| 29 | IOCTL test tool (15 testų) | ✅ test-gpu-ioctls.c |
| 30 | Vulkan ICD test | ✅ simple_test.exe |
| 31 | D3D9 triangle test | ✅ test-d3d9-triangle.c |

### Documentation

| # | Komponentas | Statusas |
|---|-------------|----------|
| 32 | README.md | ✅ Atnaujinta |
| 33 | QUICK-START.md | ✅ Atnaujinta |
| 34 | STATUS.md | ✅ Šis failas |

---

## ⏳ Ką darome DABAR

### D3D9 UMD tobulinimas (kritinis)
| Užduotis | Statusas | Svarba | Pastabos |
|----------|----------|--------|----------|
| D3D9 GetCaps grąžina realius duomenis | ✅ Padaryta | 🔴 | D3DCAPS9 su RDNA2 savybėmis |
| OpenAdapter exportas su pfnGetCaps | ✅ Padaryta | 🔴 | Trūko GetCaps, runtime negavo capsų |
| SubmitCommands IOCTL: IB packet | ✅ Padaryta | 🔴 | KMD dabar rašo INDIRECT_BUFFER į ringą |
| Flush: teisingas GPU VA | ✅ Padaryta | 🔴 | Buvo CPU adresas, dabar fizinis adresas |
| CreateResource/Lock IOCTL parametrai | ✅ Padaryta | 🔴 | Neteisingi SizeHi/SizeLo, ULONG vs ULONG64 |
| Shader compilation (DXBC → GFX10) | ⏳ Laukia | 🔴 | ACO wrapper stub |
| Texture management | ⏳ Laukia | 🟡 | |
| Render target setup | ⏳ Laukia | 🟡 | |
| ioctl.h IOCTL_INDEX fix | ⏳ Laukia | 🟡 | KMD naudoja 0x200, header turi 0x800 |

---

## 📋 Planai (ateitis)

### Trumpas laikotarpis (1-2 savaitės)
1. **Testuoti D3D9 triadą** po GetCaps + OpenAdapter fix
2. **Vulkan ICD patobulinimas** - daugiau extensions ir features
3. **DXBC → GFX10 shader kompiliatorius** - realus ACO wrapper

### Vidutinis laikotarpis (2-4 savaitės)
1. **D3D11 UMD** - D3D11 palaikymas
2. **D3D12 UMD pilnas** - D3D12 palaikymas
3. **Display mode enumeration** - daugiau rezoliucijų
4. **Multi-monitor** - 4 display pipes palaikymas

### Ilgas laikotarpis (1+ mėnesiai)
1. **OpenGL ICD** - Mesa radeonsi portas
2. **Ray tracing** - RT core palaikymas
3. **GPU compute** - SDMA compute queue
4. **VKD3D-Proton** - D3D12 → Vulkan translation

---

## 📊 Failų statistika

| Komponentas | Failai | Bendras dydis |
|-------------|--------|---------------|
| KMD source | 4 .c + 4 .h | ~220 KB |
| UMD source | 1 .c | ~55 KB |
| Vulkan ICD | 3 .c + 3 .h + 1 .def | ~65 KB |
| Build output | 7 failai | ~300 KB |
| Test tools | 5 failai | ~430 KB |
| Docs | 4 .md | ~60 KB |
| **Iš viso** | **~30 failų** | **~1.1 MB** |

---

## 🔧 Techninė architektūra

```
┌─────────────────────────────────────────────────────┐
│              D3D9/10/11/12 Application               │
├─────────────────────────────────────────────────────┤
│         Windows D3D9 Runtime (d3d9.dll)              │
├─────────────────────────────────────────────────────┤
│         BC-250 D3D9 UMD (amdbc250umd64.dll)         │
│         ├── 45+ DDI functions                        │
│         ├── DrawPrimitive → PM4 packets              │
│         ├── GetCaps → D3DCAPS9 (RDNA2)              │
│         └── Present/Flush → KMD IOCTL                │
├─────────────────────────────────────────────────────┤
│         BC-250 Vulkan ICD (amdbc250vulkan.dll)       │
│         ├── Shader compilation (SPIR-V → GFX10)     │
│         ├── QueueSubmit → KMD IOCTL                  │
│         └── QueuePresentKHR → display flip           │
├─────────────────────────────────────────────────────┤
│         ACO Shader Compiler (Mesa)                   │
│         └── SPIR-V → GFX10 ISA                      │
├─────────────────────────────────────────────────────┤
│         BC-250 KMD (atikmdag.sys)                    │
│         ├── 24 IOCTL handlers                       │
│         ├── PM4 Command Queue + IB packets           │
│         ├── Ring buffers + interrupts                │
│         ├── SDMA copy engine                         │
│         ├── 40 CU unlock                             │
│         └── DCN 2.1 display engine                   │
├─────────────────────────────────────────────────────┤
│              AMD BC-250 GPU (RDNA2)                  │
└─────────────────────────────────────────────────────┘
```

---

## 🎯 Kitas žingsnis

**Testuoti D3D9 triadą** po šių pataisymų:
1. Perkrauti kompiuterį
2. Įdiegti naują driverį (output\ per Device Manager)
3. Paleisti `test-d3d9-triangle.exe` (OutputDebugString sekimas)
4. Jei veikia - tobulinti shader kompiliaciją ir state valdymą

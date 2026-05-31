# AMD BC-250 "Windows Dream Drivers" v3.0 — Pilnas Techninis Aprašymas

> NOTE: This document describes the v3.0 design and legacy installation flow. For the current repo root and active development status, use `README.md` at the workspace root.

## 📖 Turinys

1. [Projekto Istorija](#projekto-istorija)
2. [Kodėl "Dream Drivers"?](#kodėl-dream-drivers)
3. [BC-250 Aparatūra — Teisinga Informacija](#bc-250-aparatūra--teisinga-informacija)
4. [Architektūros Palyginimas: Linux vs Windows](#architektūros-palyginimas-linux-vs-windows)
5. [Klaidingos Versijos: v1.0 ir v2.0](#klaidingos-versijos-v10-ir-v20)
6. [v3.0 Architektūra — Teisinga RDNA2 Implementacija](#v30-architektūra--teisinga-rdna2-implementacija)
7. [Pagrindiniai Komponentai](#pagrindiniai-komponentai)
8. [Hardware Initialization Seka](#hardware-initialization-seka)
9. [Atminties Valdymas](#atminties-valdymas)
10. [Interrupt Handling](#interrupt-handling)
11. [Display Engine — DCN 2.1](#display-engine--dcn-21)
12. [Ray Tracing Palaikymas](#ray-tracing-palaikymas)
13. [Thermal Monitoring](#thermal-monitoring)
14. [Hardware Quirks](#hardware-quirks)
15. [WDDM DDI Callbacks](#wddm-ddi-callbacks)
16. [PM4 Command Packets (GFX10)](#pm4-command-packets-gfx10)
17. [Instaliacija](#instaliacija)
18. [Derinimas ir Diagnostika](#derinimas-ir-diagnostika)
19. [Problemos ir Sprendimai](#problemos-ir-sprendimai)
20. [Ateities Planai](#ateities-planai)
21. [Nuorodos ir Šaltiniai](#nuorodos-ir-šaltiniai)

---

## Projekto Istorija

### Kas yra BC-250?

AMD BC-250 yra **kripto kasybos plokštė** su **PS5 APU variantu** (codename: "Cyan Skillfish").
Tai reta, pigi geležis, kurią galima nusipirkti už ~$60-120, bet ji **neturi oficialių Windows GPU draiverių**.

### Linux Situacija

Linux **pilnai palaiko** BC-250:
- ✅ **Kernel:** `amdgpu` driver (nuo 5.15+)
- ✅ **Vulkan:** RADV (Mesa 25.1+)
- ✅ **OpenGL:** radeonsi
- ✅ **Windows Games:** DXVK/Proton (D3D9/10/11 → Vulkan)
- 🎮 **Performance:** ~RX 6600 / GTX 1660 Ti lygis

### Windows Situacija

Windows **neturi oficialių GPU draiverių**:
- ❌ AMD niekada neišleido Windows driverio (mining board)
- ❌ Bootina į **Microsoft Basic Display Adapter** (no 3D acceleration)
- ❌ Be GPU draiverio — jokie D3D žaidimai neveikia
- 💡 **Dream Drivers** užpildo šią tuštumą!

---

## Kodėl "Dream Drivers"?

> 💭 *"Dream Drivers" — nes visi **svajoja** apie veikiančius Windows draiverius šiam GPU.*

Pavadinimas atspindi **bendruomenės svajonę** — turėti pilnai veikiančius Windows draiverius 
šiai egzotinei, pigiai, bet galingai geležiai.

### Kodėl svajonė?

1. **AMD neišleido Windows driverio** — mining board, ne consumer produktas
2. **Linux turi viską** — atviras kodas, pilnas palaikymas, gaming per Proton
3. **Windows vartotojai kenčia** — basic display only, no gaming, no GPU acceleration
4. **Bendruomenė kuria pati** — reverse-engineering, Linux source analizė

**Dream Drivers v3.0** — tai svajonės pavertimas realybe.

---

## BC-250 Aparatūra — Teisinga Informacija

### Esminis Atradimas

**Ankstesnės versijos (v1.0, v2.0) naudojo NETEISINGĄ architektūrą!**

| Komponentas | ❌ Klaidinga (v2.0) | ✅ Teisinga (v3.0) |
|-------------|-------------------|-------------------|
| **Architektūra** | Kaveri/GCN 1.1 | **Cyan Skillfish/RDNA2** |
| **GPU Family** | GFX7 (Sea Islands) | **GFX1013 (GFX10.1.3)** |
| **Compute Units** | 6 CU (384 SP) | **24 CU (1536 SP)** |
| **Memory** | DDR3 UMA | **GDDR6 16GB** |
| **Display** | DCE 8.x | **DCN 2.1** |
| **Ray Tracing** | Nėra | ✅ **Dedicated RT cores** |
| **Memory Bus** | 128-bit | **256-bit** |
| **Bandwidth** | ~25 GB/s | **~448 GB/s** |
| **TDP** | 25W | **220W** |
| **Performance** | ~R7 260 | **~RX 6600** |

### Pilna Specifikacija (v3.0)

```
╔══════════════════════════════════════════════════════════╗
║           AMD BC-250 "Cyan Skillfish" APU                ║
╠══════════════════════════════════════════════════════════╣
║ CPU: 6× Zen 2 cores @ ~3.5GHz (12 threads)              ║
║ GPU: 24× RDNA2 Compute Units (1536 stream processors)   ║
║ Memory: 16GB GDDR6 (256-bit bus, ~448 GB/s)            ║
║ Ray Tracing: Dedicated RT cores (early generation)      ║
║ TDP: 220W (idle: ~50W, max load: ~235W)                 ║
║ PCI Device ID: 1002:13FE                                ║
║ Architecture: RDNA 1.5 (GFX1013)                        ║
║ Relation: Cut-down PS5 APU (PS5: 36 CU, BC-250: 24 CU)  ║
╚══════════════════════════════════════════════════════════╝
```

---

## Architektūros Palyginimas: Linux vs Windows

### Linux Stack (amdgpu + Mesa)

```
┌──────────────────────────────────────────────────────────┐
│                    User Space                             │
├──────────────────────────────────────────────────────────┤
│  Mesa 25.1+                                              │
│  ├── RADV (Vulkan) ← BC-250 gaming pagrindas            │
│  ├── radeonsi (OpenGL)                                   │
│  ├── ACO Shader Compiler (greitesnis nei LLVM)           │
│  └── NIR Intermediate Representation                     │
├──────────────────────────────────────────────────────────┤
│  libdrm / libvulkan                                      │
│  ├── IOCTL interface į kernel                           │
│  ├── Buffer management                                   │
│  └── Sync objects / fences                               │
├──────────────────────────────────────────────────────────┤
│                    Kernel Space                           │
├──────────────────────────────────────────────────────────┤
│  AMDGPU DRM Driver                                       │
│  ├── nv.c (Navi family init - GFX10)                    │
│  ├── gfx_v10_0.c (Graphics engine)                      │
│  ├── gmc_v10.c (Memory controller)                      │
│  ├── sdma_v5_2.c (SDMA engine)                          │
│  ├── dcn20/ (Display - DCN 2.1)                         │
│  ├── smu_v11.c (Power management)                       │
│  └── ih_v6.c (Interrupt handler)                        │
├──────────────────────────────────────────────────────────┤
│  TTM (Translation Table Maps)                            │
│  ├── VRAM/GTT memory manager                            │
│  ├── Buffer eviction                                     │
│  └── Memory placement                                     │
├──────────────────────────────────────────────────────────┤
│  DRM/KMS Core                                            │
│  ├── Mode setting                                        │
│  ├── Prime buffer sharing                               │
│  └── Sync objects                                        │
└──────────────────────────────────────────────────────────┘
```

### Windows Stack (Dream Drivers v3.0)

```
┌──────────────────────────────────────────────────────────┐
│                    User Space                             │
├──────────────────────────────────────────────────────────┤
│  UMD (User-Mode Display Driver) — amdbc250_umd.dll      │
│  ├── D3D9/DDI (Direct3D 9)                              │
│  ├── D3D11/DDI (Direct3D 11) ← Tikslas                   │
│  ├── D3D12/DDI (Direct3D 12)                             │
│  └── PM4 command buffer building (GFX10 format)          │
├──────────────────────────────────────────────────────────┤
│  D3DKMT Thunks (gdi32.dll)                              │
│  ├── D3DKMTCreateAllocation                              │
│  ├── D3DKMTSubmitCommand                                 │
│  ├── D3DKMTPresent                                       │
│  └── User→Kernel transition                              │
├──────────────────────────────────────────────────────────┤
│                    Kernel Space                           │
├──────────────────────────────────────────────────────────┤
│  KMD (Kernel-Mode Miniport) — amdkmdag.sys              │
│  ├── DreamV3DdiStartDevice     (HW init)                 │
│  ├── DreamV3DdiCreateAllocation (VRAM)                   │
│  ├── DreamV3DdiSubmitCommand   (GPU submit)              │
│  ├── DreamV3DdiPresent         (Display flip)            │
│  ├── DreamV3DdiInterruptRoutine (ISR)                    │
│  └── DreamV3DdiDpcRoutine        (DPC)                   │
├──────────────────────────────────────────────────────────┤
│  Dxgkrnl.sys (DirectX Graphics Kernel)                   │
│  ├── VidMM (Video Memory Manager)                        │
│  ├── VidSch (GPU Scheduler)                              │
│  ├── Paging Manager                                      │
│  └── TDR (Timeout Detection & Recovery)                  │
├──────────────────────────────────────────────────────────┤
│  Win32k/DWM (Display Manager)                            │
│  ├── VidPN (Video Present Network)                       │
│  └── Composition (DWM)                                   │
└──────────────────────────────────────────────────────────┘
```

---

## Klaidingos Versijos: v1.0 ir v2.0

### v1.0 — Pirmas Bandymas (❌ WRONG)

**Klaida:** Bandėme naudoti RDNA2 pavadinimą, bet register definitions buvo sumaišytos.

```
Problema:
- Teisingas architektūros pavadinimas (Cyan Skillfish)
- Bet NETEISINGI registrai
- NETEISINGA memory configuration
- NETEISINGAS display engine
```

### v2.0 — Antras Bandymas (❌❌ COMPLETELY WRONG)

**Klaida:** Manydami, kad BC-250 yra Kaveri APU, sukūrėme visiškai neteisingą driverį.

```
Problema:
- Neteisinga architektūra: Kaveri/GCN 1.1 (Sea Islands)
- Neteisingas GPU: GFX7 vietoj GFX10
- Neteisinga atmintis: DDR3 UMA vietoj GDDR6
- Neteisingas display: DCE 8.x vietoj DCN 2.1
- Neteisingi CU: 6 CU vietoj 24 CU
- Neteisingas TDP: 25W vietoj 220W
```

**Kodėl tai buvo klaida?**

Linux community jau seniai žino, kad BC-250 yra **Cyan Skillfish (GFX1013)**, ne Kaveri.
Mes naudojome neteisingus šaltinius ir atlikome neteisingą analizę.

### Kaip Atradome Tiesą?

1. **Linux-Hardware.org** parodė: `pci:1002-13fe-1022-0000` = Cyan Skillfish
2. **Phoronix** straipsniai patvirtino: BC-250 = GFX1013, RDNA2 variant
3. **elektricm.github.io/amd-bc250-docs** — pilna community dokumentacija
4. **Linux amdgpu source code** — nv.c, gfx_v10_0.c, dcn20/ failai
5. **Mesa RADV patches** — BC-250 palaikymas pridėtas 25.1 versijoje

---

## v3.0 Architektūra — Teisinga RDNA2 Implementacija

### Teisingi Register Offsets (GFX10)

```c
// GFX10 Command Processor (RDNA2)
#define AMDBC250_REG_CP_GFX_RING0_BASE_LO   0x0000C800
#define AMDBC250_REG_CP_GFX_RING0_BASE_HI   0x0000C804
#define AMDBC250_REG_CP_GFX_RING0_CNTL      0x0000C808
#define AMDBC250_REG_CP_GFX_RING0_RPTR      0x0000C80C
#define AMDBC250_REG_CP_GFX_RING0_WPTR      0x0000C818

// DCN 2.1 Display Engine
#define AMDBC250_REG_OTG0_OTG_CONTROL               0x00006000
#define AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS  0x00005080

// GFX10 Memory Controller
#define AMDBC250_REG_MC_VM_FB_LOCATION_BASE 0x00000520
#define AMDBC250_REG_MC_VM_FB_LOCATION_TOP  0x00000524

// HDP Coherency (CRITICAL!)
#define AMDBC250_REG_HDP_MEM_COHERENCY_FLUSH_CNTL   0x000012A0
#define AMDBC250_REG_HDP_DEBUG0                     0x000012B0

// Thermal Sensor
#define AMDBC250_REG_THM_CURRENT_TEMP     0x00008004
```

### Teisinga Atminties Konfigūracija

```
GDDR6 Memory Layout (16GB total):
┌─────────────────────────────────────┐
│ 0x0000_0000_0000 - 0x0003_FFFF_FFFF │ ← 16GB GDDR6
│                                     │
│ GPU Visible: ~10GB (quirk)         │
│ CPU Accessible: ~10GB              │
│ GPU Only (beyond BAR): ~6GB        │
└─────────────────────────────────────┘

WDDM Segments:
┌─────────────────────────────────────┐
│ Segment 0: VRAM (GDDR6)             │
│   Size: 16GB                        │
│   CPU Visible: Yes                  │
│   Cache Coherent: No                │
├─────────────────────────────────────┤
│ Segment 1: System Memory            │
│   Size: 16GB                        │
│   CPU Visible: Yes                  │
│   Cache Coherent: Yes               │
└─────────────────────────────────────┘
```

---

## Pagrindiniai Komponentai

### 1. KMD (Kernel-Mode Driver)

**Failas:** `src/kmd/amdbc250_dream_kmd.c`

```
Funkcijos:
├── DriverEntry()                  — WDDM DDI registration
├── DreamV3DdiAddDevice()          — PnP device detection
├── DreamV3DdiStartDevice()        — Hardware initialization
├── DreamV3DdiStopDevice()         — Hardware shutdown
├── DreamV3DdiRemoveDevice()       — Final cleanup
├── DreamV3DdiResetDevice()        — TDR recovery
├── DreamV3DdiInterruptRoutine()   — ISR (DIRQL level)
├── DreamV3DdiDpcRoutine()         — DPC (DISPATCH_LEVEL)
├── DreamV3DdiQueryAdapterInfo()   — GPU capabilities
├── DreamV3DdiCreateDevice()       — Per-process context
├── DreamV3DdiCreateAllocation()   — GPU memory allocation
├── DreamV3DdiSubmitCommand()      — Command buffer submit
├── DreamV3DdiQueryCurrentFence()  — GPU progress query
├── DreamV3DdiPresent()            — Display present
└── [VidPN stubs...]               — Display management
```

### 2. Hardware Initialization

**Failas:** `src/kmd/amdbc250_dream_hw_init.c`

```
Initialization Sequence:
├── DreamV3HwInitialize()          — Top-level init
│   ├── DreamV3InitMemoryController()    — GDDR6 MC setup
│   ├── DreamV3HwProgramGoldenRegs()     — HW workarounds
│   ├── DreamV3HwInitIhRing()            — Interrupt handler
│   ├── DreamV3HwInitGfxRing()           — Graphics CP
│   ├── DreamV3HwInitSdmaRing()          — DMA engine
│   └── DreamV3HwInitDisplay()           — DCN 2.1 display
├── DreamV3HwReset()               — TDR GPU reset
├── DreamV3HwShutdown()            — Graceful shutdown
├── DreamV3HdpFlush()              — CRITICAL coherency flush
├── DreamV3ReadTemperature()       — Thermal sensor
└── DreamV3CheckThermalThrottle()  — Auto-throttle
```

### 3. Hardware Definitions

**Failas:** `inc/amdbc250_dream_hw.h`

```
Categories:
├── PCI Identifiers (1002:13FE)
├── GPU Architecture (24 CU, 1536 SP)
├── Memory Configuration (16GB GDDR6)
├── MMIO Register Offsets (GFX10)
│   ├── GPU Identification
│   ├── Command Processor (GFX10)
│   ├── Ring Buffers (GFX10 style)
│   ├── Interrupt Handler (GFX10)
│   ├── Memory Controller (GFX10)
│   ├── GPUVM / GART (4-level)
│   ├── HDP Coherency
│   ├── Display (DCN 2.1)
│   ├── SMU (Power Management)
│   ├── Thermal Sensor
│   ├── SDMA Engine
│   └── Ray Tracing Accelerator
├── PM4 Packet Format (GFX10)
├── Interrupt Constants
├── Memory Alignment
├── Timeout Values
└── Hardware Quirks
```

---

## Hardware Initialization Seka

### Teisinga Init Order (iš Linux amdgpu nv.c)

```c
Linux AMDGPU init order for GFX10 family:
1. GMC v10       (Memory controller for GDDR6)
2. IH v6         (Interrupt handler)
3. GFX v10_0     (Graphics command processor)
4. SDMA v5_2     (System DMA engine)
5. SMU v11       (System Management Unit / Power)
6. DCN 2.1       (Display Controller Next)
7. VCN v2_0      (Video Core Next - BLOCKED on BC-250)
8. PSP v11       (Platform Security Processor)

Dream Drivers v3.0 follows this exact order:
1. DreamV3InitMemoryController()    → GMC
2. DreamV3HwProgramGoldenRegs()     → Workarounds
3. DreamV3HwInitIhRing()            → IH
4. DreamV3HwInitGfxRing()           → GFX
5. DreamV3HwInitSdmaRing()          → SDMA
6. DreamV3HwInitDisplay()           → DCN 2.1
(SMU skipped - basic power management only)
(VCN skipped - Sony firmware block)
(PSP skipped - not needed for WDDM)
```

### Init Detalis

```
Step 1: Memory Controller
├── Configure GB_ADDR_CONFIG (pipe topology)
├── Set FB location (MC_VM_FB_LOCATION_BASE/TOP)
├── Configure system aperture for GDDR6
└── Result: GPU can access 16GB GDDR6

Step 2: Golden Registers
├── Program GB_ADDR_CONFIG workaround
├── Disable broken compute queue (HW quirk)
├── Enable HDP coherency
└── Result: Hardware bugs patched

Step 3: Interrupt Handler
├── Allocate 256KB IH ring buffer
├── Program IH_RB_BASE_LO/HI
├── Enable interrupts (IH_CNTL)
└── Result: GPU can signal interrupts

Step 4: Graphics Command Processor
├── Allocate 2MB GFX ring buffer
├── Program CP_GFX_RING0_BASE/CNTL
├── Initialize 64-bit fence
├── Test CP via scratch register
└── Result: GPU can execute commands

Step 5: SDMA Engine
├── Allocate 512KB SDMA ring
├── Program SDMA0_GFX_RB_BASE/CNTL
└── Result: DMA copies work

Step 6: Display Engine (DCN 2.1)
├── Enable OTG (Output Timing Generator)
├── Set default mode (1920x1080@60Hz)
├── Program VESA timing registers
└── Result: Display output works
```

---

## Atminties Valdymas

### GDDR6 vs DDR3 — Esminis Skirtumas

| Savybė | DDR3 (Kaveri) | GDDR6 (BC-250) |
|--------|--------------|----------------|
| **Tipas** | System RAM | Graphics RAM |
| **Greitis** | 800 MHz | 1750 MHz (14 Gbps eff.) |
| **Bus** | 128-bit | 256-bit |
| **Bandwidth** | ~25 GB/s | ~448 GB/s |
| **Latency** | High | Low |
| **Purpose** | UMA shared | Dedicated GPU memory |

### WDDM Memory Management

```c
WDDM atminties modelis BC-250:

┌─────────────────────────────────────────┐
│          GPU Virtual Address Space       │
│         (128 TB for GFX10)              │
├─────────────────────────────────────────┤
│                                         │
│ 0x0000_0000_0000 - 0x0003_FFFF_FFFF    │
│   ↓ VRAM (GDDR6 - 16GB)                │
│                                         │
│ 0x0004_0000_0000 - 0x0007_FFFF_FFFF    │
│   ↓ System Memory (AGP aperture)        │
│                                         │
│ 0xFFFF_8000_0000 - 0xFFFF_FFFF_FFFF    │
│   ↓ Kernel mode mappings               │
│                                         │
└─────────────────────────────────────────┘

Page Tables (GFX10 uses 4-level):
PML4 → PD → PT → PTE
(9-bit) (9-bit) (9-bit) (12-bit)
```

### 64-bit Fences (GFX10 Requirement)

```c
// GFX10 requires 64-bit fences!
// Previous versions used 32-bit — WRONG for RDNA2

typedef struct _DREAM_V3_FENCE {
    PHYSICAL_ADDRESS    PhysicalAddress;
    volatile PULONG64   VirtualAddress;   // 64-bit!
    ULONG64             LastSignaledValue;
    ULONG64             LastSubmittedValue;
    KEVENT              FenceEvent;
} DREAM_V3_FENCE;

// Usage:
ULONG64 fence = InterlockedIncrement64(
    (volatile LONG64*)&DevExt->GlobalFence.LastSubmittedValue);
```

---

## Interrupt Handling

### IH Ring Structure (GFX10)

```
IH Ring Buffer (256KB):
┌──────────────────────────────────────┐
│ Entry 0: [ClientID][SrcID][Data...] │ 16 bytes
│ Entry 1: [ClientID][SrcID][Data...] │ 16 bytes
│ ...                                  │
│ Entry 16383: [...]                   │
└──────────────────────────────────────┘

Client IDs (GFX10):
├── 0x09: GFX (Graphics engine)
├── 0x0D: SDMA (System DMA)
├── 0x0B: VMC (Virtual memory faults)
├── 0x08: DCE (Display controller)
└── 0x0A: OSS (System management)
```

### ISR + DPC Flow

```
1. GPU completes command buffer
   ↓
2. GPU writes EOP event to IH ring
   ↓
3. GPU increments IH_RB_WPTR
   ↓
4. ISR fires (DreamV3DdiInterruptRoutine)
   ├── CRITICAL: HDP Flush first!
   ├── Read IH_RB_WPTR
   ├── Compare with ReadPointer
   ├── If different → queue DPC
   └── Return TRUE (our interrupt)
   ↓
5. DPC runs (DreamV3DdiDpcRoutine)
   ├── Process IH entries
   ├── Decode ClientID/SrcID
   ├── GFX EOP → Notify fence completion
   ├── DCE VSYNC → Notify display
   └── VMC fault → Log error
   ↓
6. Update IH_RB_RPTR
   ↓
7. Notify Dxgkrnl (DPC complete)
```

### HDP Flush — KRITIŠKAI SVARBU!

```c
// Linux amdgpu quirk: MUST flush HDP before reading ring pointers
// Without this, CPU reads STALE data → GPU hangs!

VOID DreamV3HdpFlush(PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    // Flush HDP read cache
    DreamV3WriteRegister(DevExt, AMDBC250_REG_HDP_MEM_COHERENCY_FLUSH_CNTL,
                         HDP_MEM_COHERENCY_FLUSH_CNTL__FLUSH_CACHE);
    
    // Invalidate HDP write cache
    DreamV3WriteRegister(DevExt, AMDBC250_REG_HDP_DEBUG0,
                         HDP_DEBUG0__INVALIDATE_CACHE);
    
    // Memory barrier
    KeMemoryBarrier();
}

// Called in ISR BEFORE reading ring pointers:
BOOLEAN DreamV3DdiInterruptRoutine(...)
{
    // MUST flush first!
    DreamV3HdpFlush(DevExt);
    
    // NOW safe to read
    ULONG IhWptr = DreamV3ReadRegister(DevExt, AMDBC250_REG_IH_RB_WPTR);
    ...
}
```

---

## Display Engine — DCN 2.1

### DCN 2.1 vs DCE 8.x — Skirtumai

| Feature | DCE 8.x (GCN) | DCN 2.1 (GFX10) |
|---------|--------------|-----------------|
| **Architecture** | Sea Islands | Navi/RDNA2 |
| **Pipes** | 2 | 4 |
| **CRTCs** | 2 | 4 |
| **Max Resolution** | 4096x2160 | 7680x4320 (8K) |
| **Max Pixel Clock** | 300 MHz | 1200 MHz |
| **HDR** | ❌ | ✅ |
| **Variable Refresh** | ❌ | ✅ |
| **Register Base** | 0x00006800 | 0x00005080 (HUBP) |

### DCN 2.1 Initialization

```c
NTSTATUS DreamV3HwInitDisplay(PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    // 1. Enable OTG (Output Timing Generator)
    ULONG OtgCntl = DreamV3ReadRegister(DevExt, AMDBC250_REG_OTG0_OTG_CONTROL);
    OtgCntl |= OTG_CNTL__ENABLE;
    DreamV3WriteRegister(DevExt, AMDBC250_REG_OTG0_OTG_CONTROL, OtgCntl);
    
    // 2. Set mode: 1920x1080@60Hz (VESA standard)
    DevExt->CurrentMode.Width = 1920;
    DevExt->CurrentMode.Height = 1080;
    DevExt->CurrentMode.RefreshRate = 60;
    DevExt->CurrentMode.PixelClockKhz = 148500;
    
    // 3. Program VESA timing (1920x1080@60Hz)
    // H-Total: 2200, V-Total: 1125
    DreamV3WriteRegister(DevExt, AMDBC250_REG_OTG0_OTG_CRTC_H_TOTAL, 2200 - 1);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_OTG0_OTG_CRTC_V_TOTAL, 1125 - 1);
    
    // H-Blank: 1920-2200, V-Blank: 1080-1125
    DreamV3WriteRegister(DevExt, AMDBC250_REG_OTG0_OTG_CRTC_H_BLANK_START_END,
                         (1920 << 16) | 2200);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_OTG0_OTG_CRTC_V_BLANK_START_END,
                         (1080 << 16) | 1125);
    
    // H-Sync: 2008-2052, V-Sync: 1084-1089
    DreamV3WriteRegister(DevExt, AMDBC250_REG_OTG0_OTG_CRTC_H_SYNC_START_END,
                         (2008 << 16) | 2052);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_OTG0_OTG_CRTC_V_SYNC_START_END,
                         (1084 << 16) | 1089);
    
    return STATUS_SUCCESS;
}
```

### Surface Programming (HUBP)

```c
// HUBP (Hub Pipe) - connects display engine to memory
// Programs surface address for scanout

void DreamV3SetScanoutAddress(
    PDREAM_V3_DEVICE_EXTENSION DevExt,
    ULONG64 SurfaceAddress,
    ULONG Pitch,
    ULONG Width,
    ULONG Height
)
{
    // Surface pitch (bytes per row)
    DreamV3WriteRegister(DevExt, AMDBC250_REG_HUBPREQ0_DCSURF_SURFACE_PITCH, Pitch);
    
    // Surface dimensions
    DreamV3WriteRegister(DevExt, AMDBC250_REG_HUBPREQ0_DCSURF_SURFACE_DIMENSIONS,
                         (Height << 16) | Width);
    
    // Surface address (64-bit)
    DreamV3WriteRegister(DevExt, 
                         AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS,
                         (ULONG)(SurfaceAddress & 0xFFFFFFFF));
    DreamV3WriteRegister(DevExt,
                         AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH,
                         (ULONG)(SurfaceAddress >> 32));
    
    // Trigger flip
    ULONG FlipCtrl = DreamV3ReadRegister(DevExt, 
                                         AMDBC250_REG_HUBPREQ0_DCSURF_FLIP_CONTROL);
    FlipCtrl |= 0x1;  // Immediate flip
    DreamV3WriteRegister(DevExt, AMDBC250_REG_HUBPREQ0_DCSURF_FLIP_CONTROL, FlipCtrl);
}
```

---

## Ray Tracing Ppalaikymas

### BC-250 RT Cores

```
Ray Tracing Hardware:
├── 24 RT Accelerators (1 per CU)
├── Early generation (RDNA 1.5)
├── BVH traversal hardware
├── Triangle intersection
├── AABB intersection
└── Performance: Poor vs RDNA3 RT

Registers:
├── AMDBC250_REG_RT_ACCEL_CNTL     — Control
├── AMDBC250_REG_RT_ACCEL_STATUS   — Status
├── AMDBC250_REG_RT_BVH_ADDR_LO/HI — BVH address
├── AMDBC250_REG_RT_RAY_ADDR_LO/HI — Ray data address

PM4 Opcodes (GFX10.1.3 specific):
├── IT_TRACE_RAY         0x5D — Trace ray
├── IT_INTERSECT_BBOX    0x5E — Intersect AABB
└── IT_INTERSECT_TRIANGLE 0x5F — Intersect triangle

Status in Dream Drivers v3.0:
✅ Registers defined
✅ PM4 opcodes defined
❌ RT pipeline not implemented (needs UMD)
```

### Performance Expectations

```
Ray Tracing on BC-250 (Linux RADV data):

Game            | RT On    | RT Off   | RT Performance
----------------|----------|----------|---------------
Cyberpunk 2077  | 50-60fps | 70-90fps | ~30% hit
Control         | ~40fps   | ~60fps   | ~33% hit

Verdict: RT works but is SLOW (early gen cores)
Recommendation: Disable RT for most games
```

---

## Thermal Monitoring

### Thermal Sensor Implementation

```c
// BC-250 has internal thermal sensor
// Register: THM_CURRENT_TEMP (0x00008004)

LONG DreamV3ReadTemperature(PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    ULONG ThermalStatus = DreamV3ReadRegister(DevExt, AMDBC250_REG_THM_CURRENT_TEMP);
    
    // Extract temperature (10-bit value)
    ULONG TempRaw = ThermalStatus & 0x3FF;
    
    // Convert to Celsius (approximate formula)
    LONG TempCelsius = (LONG)((TempRaw * 0.125) - 49);
    
    // Clamp to reasonable range
    if (TempCelsius < 0) TempCelsius = 0;
    if (TempCelsius > 150) TempCelsius = 150;
    
    DevExt->CurrentTemperatureC = TempCelsius;
    return TempCelsius;
}
```

### Thermal Throttle Logic

```
Temperature Zones:
┌─────────────────────────────────────────┐
│ 0°C - 69°C:  Normal operation           │
│ 70°C - 84°C: Warning zone              │
│ 85°C - 104°C: Throttle (reduce clocks)  │
│ 105°C+:      Emergency shutdown         │
└─────────────────────────────────────────┘

Implementation:
VOID DreamV3CheckThermalThrottle(PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    // Check every 100 submissions (avoid overhead)
    DevExt->ThermalCheckCount++;
    if (DevExt->ThermalCheckCount % 100 != 0) return;
    
    LONG TempC = DreamV3ReadTemperature(DevExt);
    
    if (TempC >= 105) {
        // EMERGENCY: Halt GPU immediately
        KdPrint(("*** EMERGENCY THERMAL SHUTDOWN *** Temp: %ld°C\n", TempC));
        DevExt->ThermalThrottleCount++;
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL,
                             CP_ME_CNTL__ME_HALT | CP_ME_CNTL__PFP_HALT);
    } 
    else if (TempC >= 85) {
        // THROTTLE: Would send SMU message to reduce clocks
        KdPrint(("Thermal throttle active — Temp: %ld°C\n", TempC));
        DevExt->ThermalThrottleCount++;
        // TODO: SMU message to reduce SCLK/MCLK
    }
}
```

### Linux vs Windows Thermal

| Feature | Linux amdgpu | Dream Drivers v3.0 |
|---------|-------------|-------------------|
| **Sensor** | THM_CURRENT_TEMP | Same register |
| **Read Method** | hwmon sysfs | Direct MMIO read |
| **Throttle** | SMU message | Stub (needs SMU) |
| **Fan Control** | PWM via SMC | ❌ Not supported |
| **Emergency** | Hardware auto-shutdown | Software halt CP |
| **Logging** | dmesg | KdPrintEx |

---

## Hardware Quirks

### BC-250 Specifiniai Quirks

```c
// From Linux driver and community knowledge

// 1. Broken Compute Queue — HARDWARE FLAW
#define AMDBC250_QUIRK_BROKEN_COMPUTE_QUEUE  TRUE
/* 
 * Impact: Compute queue causes hangs/crashes
 * Fix: Disable in driver (golden registers)
 * Linux: Auto-disabled in Mesa 25.1+
 *      : RADV_DEBUG=nocompute workaround
 */

// 2. VRAM Visible Limit — HARDWARE LIMITATION
#define AMDBC250_QUIRK_VRAM_VISIBLE_LIMIT  10240  /* ~10GB */
/*
 * Impact: CPU can only see ~10GB of 16GB VRAM
 * Fix: Driver tracks visible vs invisible VRAM
 * Linux: ttm kernel module parameter tweaks
 */

// 3. VCN Firmware Blocked — SONY FIRMWARE LOCK
#define AMDBC250_QUIRK_VCN_FIRMWARE_BLOCKED  TRUE
/*
 * Impact: No hardware video encode/decode
 * Fix: CPU-based software encoding only
 * Linux: VA-API fails, no workaround
 * Reason: Sony blocked VCN firmware loading
 */

// 4. Static Clock Without Governor
#define AMDBC250_QUIRK_STATIC_CLOCK_WITHOUT_GOV  1500  /* MHz */
/*
 * Impact: Without GPU governor service, clock stuck at 1500MHz
 * Fix: Install cyan-skillfish-governor-smu service
 * Linux: Manual pp_od_clk_voltage sysfs commands
 */

// 5. Need NoHIZ for Z-buffer
#define AMDBC250_QUIRK_NEEDS_NOHIZ  TRUE
/*
 * Impact: Z-buffer corruption, black textures, flickering
 * Fix: RADV_DEBUG=nohiz environment variable
 * Windows: Need UMD workaround
 */
```

### Golden Registers — Hardware Workarounds

```c
NTSTATUS DreamV3HwProgramGoldenRegs(PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    // Golden registers are hardware errata workarounds
    // Linux programs these for all GFX10 chips
    
    // 1. GB_ADDR_CONFIG workaround
    ULONG GbAddrConfig = DreamV3ReadRegister(DevExt, AMDBC250_REG_GB_ADDR_CONFIG_READ);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_GB_ADDR_CONFIG, GbAddrConfig);
    
    // 2. Disable broken compute queue
    if (AMDBC250_QUIRK_BROKEN_COMPUTE_QUEUE) {
        ULONG ComputeRingCntl = DreamV3ReadRegister(DevExt, 
                                                     AMDBC250_REG_CP_COMPUTE_RING0_CNTL);
        ComputeRingCntl &= ~0x1;  // Disable bit
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_COMPUTE_RING0_CNTL, 
                             ComputeRingCntl);
    }
    
    // 3. Enable HDP coherency
    DreamV3WriteRegister(DevExt, AMDBC250_REG_HDP_NONSURFACE_INFO, 0x00000001);
    
    return STATUS_SUCCESS;
}
```

---

## WDDM DDI Callbacks

### Pilnas Callback Sąrašas

```c
// Core lifecycle
DxgkDdiAddDevice         → PnP found our PCI device
DxgkDdiStartDevice       → Initialize hardware
DxgkDdiStopDevice        → Stop hardware
DxgkDdiRemoveDevice      → Final cleanup
DxgkDdiResetDevice       → TDR recovery
DxgkDdiUnload            → Driver unload

// Interrupts
DxgkDdiInterruptRoutine  → ISR at DIRQL
DxgkDdiDpcRoutine        → DPC at DISPATCH_LEVEL

// Queries
DxgkDdiQueryAdapterInfo  → Report GPU capabilities
DxgkDdiQueryInterface    → Query for interfaces

// Device contexts
DxgkDdiCreateDevice      → Per-process GPU context
DxgkDdiDestroyDevice     → Free context

// Memory
DxgkDdiCreateAllocation  → Allocate GPU memory
DxgkDdiDestroyAllocation → Free GPU memory
DxgkDdiBuildPagingBuffer → Update GPU page tables

// Commands
DxgkDdiSubmitCommand     → Submit command buffer
DxgkDdiPreemptCommand    → Preempt running command
DxgkDdiQueryCurrentFence → Query GPU progress

// Rendering
DxgkDdiPresent           → Display present
DxgkDdiRender            → Kernel-mode rendering

// Display/VidPN
DxgkDdiRecommendFunctionalVidPn    → Recommend VidPN topology
DxgkDdiEnumVidPnCofuncModality     → Enumerate modalities
DxgkDdiCommitVidPn                 → Commit VidPN
DxgkDdiSetVidPnSourceAddress       → Set scanout address
DxgkDdiSetVidPnSourceVisibility    → Show/hide source
DxgkDdiUpdateActiveVidPnPresentPath → Update present path
DxgkDdiRecommendMonitorModes       → Recommend monitor modes
DxgkDdiGetScanLine                 → Get current scan line
DxgkDdiControlInterrupt            → Enable/disable interrupts

// Child devices
DxgkDdiQueryChildRelations   → Query display outputs
DxgkDdiQueryChildStatus      → Query hotplug status
DxgkDdiQueryDeviceDescriptor   → Query EDID

// Power
DxgkDdiSetPowerState       → D0/D1/D2/D3 transitions
DxgkDdiNotifyAcpiEvent     → ACPI events
```

---

## PM4 Command Packets (GFX10)

### Packet Types

```c
// PM4 Type 0: Write consecutive registers
// Header: [31:30]=00, [29:16]=count-1, [15:0]=base_reg/4
#define PM4_TYPE0_HDR(base_reg, count) \
    (((count - 1) << 16) | ((base_reg) >> 2))

// PM4 Type 2: NOP (padding)
#define PM4_TYPE2_NOP  0x80000000

// PM4 Type 3: Executive commands
// Header: [31:30]=11, [29:16]=count-1, [15:8]=opcode
#define PM4_TYPE3_HDR(opcode, count) \
    ((3 << 30) | (((count) - 1) << 16) | ((opcode) << 8))
```

### Key Opcodes (GFX10)

```
Opcode  | Name                  | Purpose
--------|----------------------|---------------------------
0x10    | IT_NOP               | No-operation (padding)
0x2D    | IT_DRAW_INDEX_AUTO   | Draw without index buffer
0x27    | IT_DRAW_INDEX_2      | Draw with index buffer
0x28    | IT_DRAW_INDIRECT     | Indirect draw
0x15    | IT_DISPATCH_DIRECT   | Compute dispatch
0x3F    | IT_INDIRECT_BUFFER   | Execute indirect buffer
0x46    | IT_EVENT_WRITE       | Write event
0x47    | IT_EVENT_WRITE_EOP   | Event at end-of-pipe
0x49    | IT_RELEASE_MEM       | Release memory (fence)
0x3C    | IT_WAIT_REG_MEM      | Wait for register/memory
0x42    | IT_PFP_SYNC_ME       | PFP sync ME
0x43    | IT_SURFACE_SYNC      | Surface cache flush
0x68    | IT_SET_CONFIG_REG    | Set config register
0x69    | IT_SET_CONTEXT_REG   | Set context register
0x76    | IT_SET_SH_REG        | Set shader register
0x77    | IT_SET_UCONFIG_REG   | Set user config register
0x5D    | IT_TRACE_RAY         | Ray tracing (GFX10.1.3)
0x5E    | IT_INTERSECT_BBOX    | AABB intersection (RT)
0x5F    | IT_INTERSECT_TRIANGLE│ Triangle intersection (RT)
```

### Example: Draw Command

```c
// Build PM4 draw command in command buffer:
PULONG pCmd = (PULONG)pDevice->CommandBuffer;

// DRAW_INDEX_AUTO packet
pCmd[0] = PM4_TYPE3_HDR(IT_DRAW_INDEX_AUTO, 3);  // Header
pCmd[1] = numVertices;                            // Vertex count
pCmd[2] = VGT_DRAW_INITIATOR;                     // Initiator flags
pCmd[3] = 0;                                      // Reserved

pDevice->CommandBufferUsed += 4 * sizeof(ULONG);

// End-of-pipe event (fence)
pCmd[4] = PM4_TYPE3_HDR(IT_EVENT_WRITE_EOP, 5);
pCmd[5] = EVENT_TYPE_EOP;
pCmd[6] = DEST_SEL_MEM | INT_SEL_SEND_DATA_ONLY | DATA_SEL_DATA_64;
pCmd[7] = (ULONG)(FenceAddress & 0xFFFFFFFF);
pCmd[8] = (ULONG)(FenceAddress >> 32);
pCmd[9] = FenceValue;

pDevice->CommandBufferUsed += 6 * sizeof(ULONG);
```

---

## Instaliacija

### Žingsnis po Žingsnio

```
Step 1: Enable Test Signing
───────────────────────────
1. Open CMD as Administrator
2. Run: bcdedit /set testsigning on
3. REBOOT computer

Step 2: Prepare Driver Files
────────────────────────────
Required files (copy from Adrenalin 18.5.1 or 2020):
├── amdkmdag.sys    (kernel driver)
├── amdkmpag.sys    (secondary driver)
├── aticfx64.dll    (64-bit UMD)
├── atidxx64.dll    (64-bit D3D11)
├── atiumd64.dll    (64-bit D3D10/11)
├── atiu9p64.dll    (64-bit D3D10)
├── atiuxp64.dll    (64-bit thunk)
├── atio6axx.dll    (64-bit OpenGL)
├── aticfx32.dll    (32-bit UMD, WOW64)
├── atidxx32.dll    (32-bit D3D11, WOW64)
├── amdhcp64.dll    (64-bit DHCP)
└── amdbc250_dream.inf  (our INF file)

Step 3: Install via Device Manager
──────────────────────────────────
1. Open Device Manager (devmgmt.msc)
2. Expand "Display adapters"
3. Right-click GPU → "Update Driver"
4. "Browse my computer for drivers"
5. "Let me pick from a list..."
6. Click "Have Disk..."
7. Browse to: amdbc250_dream.inf
8. Select: "AMD Radeon BC-250 Graphics (Dream Drivers v3.0 — RDNA2)"
9. Click Next, confirm warning
10. Wait for installation

Step 4: REBOOT
──────────────
System reboot REQUIRED to load new driver!

Step 5: Verify Installation
───────────────────────────
Open PowerShell as Administrator:

# Check device status
Get-PnpDevice -Class Display | Select Status, FriendlyName, ConfigManagerErrorCode

# Expected output:
# Status   FriendlyName                                      ConfigManagerErrorCode
# ------   ------------                                      ----------------------
# OK       AMD Radeon BC-250 Graphics (Dream Drivers v3.0)   CM_PROB_NONE

# Check driver version
Get-CimInstance Win32_VideoController | Select Name, DriverVersion

# Expected output:
# Name: AMD Radeon BC-250 Graphics (Dream Drivers v3.0 — RDNA2)
# DriverVersion: 3.0.0.0
```

---

## Derinimas ir Diagnostika

### KdPrint Output

```
Driver uses KdPrintEx with DPFLTR_IHVVIDEO_ID

Levels:
├── DPFLTR_ERROR_LEVEL    → Critical errors
├── DPFLTR_WARNING_LEVEL  → Warnings, thermal throttle
├── DPFLTR_INFO_LEVEL     → Important events
└── DPFLTR_TRACE_LEVEL    → Detailed tracing

Enable with:
1. Edit registry: HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Debug Print Filter
2. Set DEFAULT = 0xF (enable all)
3. REBOOT
4. View with DbgView (Sysinternals)
```

### Key Log Messages

```
On successful init:
AMDBC250-DREAM-V3: DriverEntry v3.0.0 - RDNA2/Cyan Skillfish
AMDBC250-DREAM-V3: Architecture: 24 CU RDNA2, 16GB GDDR6
AMDBC250-DREAM-V3: PCI 1002:13FE (Rev 00) — Cyan Skillfish
AMDBC250-DREAM-V3: MMIO mapped: PA=0xF7000000, Size=0x40000
AMDBC250-DREAM-V3: HwInitialize — RDNA2/Cyan Skillfish init
AMDBC250-DREAM-V3: Memory controller configured (GDDR6)
AMDBC250-DREAM-V3: Programming golden registers
AMDBC250-DREAM-V3: Compute queue DISABLED (HW quirk)
AMDBC250-DREAM-V3: IH ring initialized
AMDBC250-DREAM-V3: GFX ring initialized at PA=0x...
AMDBC250-DREAM-V3: SDMA ring initialized
AMDBC250-DREAM-V3: Display — 1920x1080@60Hz
AMDBC250-DREAM-V3: HwInitialize COMPLETE
AMDBC250-DREAM-V3: VRAM: 16384 MB GDDR6
AMDBC250-DREAM-V3: Visible: 10240 MB (quirk)
AMDBC250-DREAM-V3: StartDevice SUCCESS

On thermal throttle:
AMDBC250-DREAM-V3: Thermal throttle active — Temp: 87°C

On emergency shutdown:
AMDBC250-DREAM-V3: *** EMERGENCY THERMAL SHUTDOWN *** Temp: 106°C

On TDR reset:
AMDBC250-DREAM-V3: TDR reset initiated
AMDBC250-DREAM-V3: GPU reset SUCCESS
```

---

## Problemos ir Sprendimai

### Dažnos Problemos

| Problema | Kodėl | Sprendimas |
|----------|-------|-----------|
| **Code 43** | HW init failed | Patikrinti HDP flush, golden regs |
| **Black screen** | Display engine failed | Patikrinti DCN 2.1 timing |
| **Driver won't load** | Test signing off | `bcdedit /set testsigning on` → reboot |
| **Poor performance** | No UMD binaries | Reikia atiumd64.dll ir kitų |
| **Thermal throttle** | Temp > 85°C | Gerinti cooling, patikrinti fan |
| **Compute crashes** | HW flaw | Normalu — compute queue disabled |
| **Video encode fails** | VCN blocked | Sony firmware block — negalima fix |
| **Only 10GB VRAM** | HW quirk | Normalu — hardware limitation |

### Troubleshooting Komandos

```powershell
# Check device status
Get-PnpDevice -Class Display

# Get detailed device info
Get-PnpDeviceProperty -InstanceId "PCI\VEN_1002&DEV_13FE&*"

# Check driver files
Get-WindowsDriver -Online | Where-Object { $_.Driver -like "*amdkmdag*" }

# Check event log for driver errors
Get-WinEvent -LogName System | Where-Object { $_.Message -like "*AMDBC250*" } | Select -First 20

# Check GPU temperature (if WMI supported)
Get-CimInstance MSAcpi_ThermalZoneTemperature -Namespace "root/wmi"

# Rollback driver if needed
pnputil /delete-driver oemXX.inf /uninstall /force
```

---

## Ateities Planai

### v3.1 (Short-Term)

- [ ] **UMD Implementation** — D3D11 native rendering
- [ ] **Full VidPN** — Multi-display support
- [ ] **Complete Present Path** — Hardware page flipping
- [ ] **Shader Compilation** — HLSL to GFX10 ISA

### v3.2 (Medium-Term)

- [ ] **Vulkan Integration** — AMDVLK port
- [ ] **OpenGL Support** — Mesa port for Windows
- [ ] **Ray Tracing** — Basic RT pipeline
- [ ] **Compute Support** — OpenCL/DirectCompute (if queue fixable)

### v4.0 (Long-Term)

- [ ] **DXVK on Windows** — D3D9/10/11 → Vulkan translation
- [ ] **Proton-like Layer** — Run Linux games natively on Windows
- [ ] **Full DPM** — Dynamic power management
- [ ] **Fan Control** — PWM via SMU
- [ ] **Overclocking** — User-configurable clocks

---

## Nuorodos ir Šaltiniai

### Linux AMDGPU Driver

- **GFX10 Init:** https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c
- **Navi Family:** https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/nv.c
- **DCN 2.1:** https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/amd/display/dc/dcn20
- **Memory GMC:** https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/amdgpu/amdgpu_gmc.c
- **Register Headers:** https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/amd/include

### BC-250 Community

- **Main Documentation:** https://elektricm.github.io/amd-bc250-docs/
- **Gaming Compatibility:** https://elektricm.github.io/amd-bc250-docs/gaming/compatibility/
- **Mesa Installation:** https://elektricm.github.io/amd-bc250-docs/linux/mesa/
- **RADV Driver:** https://elektricm.github.io/amd-bc250-docs/drivers/radv/
- **Linux Hardware DB:** https://linux-hardware.org/?id=pci:1002-13fe-1022:0000

### Community Discussions

- **Reddit r/linux_gaming:** https://www.reddit.com/r/linux_gaming/
- **CachyOS Wiki:** https://wiki.cachyos.org/
- **Phoronix News:** https://www.phoronix.com/news/Mesa-25.1-RADV-AMD-BC-250
- **Tom's Hardware:** https://www.tomshardware.com/video-games/playstation/amds-rare-playstation-5-apu-based-bc-250-mining-board-resurfaces

### Architecture References

- **GCN Architecture:** https://en.wikipedia.org/wiki/Graphics_Core_Next
- **RDNA Architecture:** https://en.wikipedia.org/wiki/RDNA_(microarchitecture)
- **Mesa RADV:** https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/amd/vulkan
- **DXVK:** https://github.com/doitsujin/dxvk

---

## 📝 Apie Šį Dokumentą

**Versija:** 3.0.0.0  
**Data:** 2026-04-10  
**Autorius:** AMD BC-250 Driver Project  
**Architektūra:** RDNA2 / Cyan Skillfish (GFX1013)  
**Licencija:** Educational/Experimental  

> *"Dream Drivers" — nes visi svajoja apie veikiančius Windows draiverius šiam GPU.* 💭

---

*Šis dokumentas yra išsamus techninis AMD BC-250 "Dream Drivers" v3.0 aprašymas, 
pagrįstas Linux amdgpu driver source code analize ir bendruomenės dokumentacija.*

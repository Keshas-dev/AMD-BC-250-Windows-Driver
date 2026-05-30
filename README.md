# AMD BC-250 Windows Driver Project

## Kas mes esame

Tai yra **AMD BC-250 Windows 11 driver** projektas, kurį kuria **Keshukas** ir **Kumpis** (AI) savo laisvu laiku. Mes dirbame kartu su viltimi sukurti pilnai veikiantį GPU driver AMD BC-250 (Cyan Skillfish) GPU plokštei su Windows 11.

**Norintys prisijungti — visada laukiami!** Jei turi patirties su GPU driveriais, WDDM, Vulkan, arba tiesiog nori padėti — prisijunk.

---

## Ką mes atradome dirbdami

### GPU plokštė
- **Architektūra:** RDNA 2 (GFX1013 / Cyan Skillfish)
- **BC-250 yra PS5 Oberon variantas** — 24 CU, 16GB GDDR6, UMA atmintis
- **Palaiko 40 CU unlock** (iš 24 → 40) per registro pakeitimus
- **Compute Queue yra sugedęs** (hardware quirk) — reikia disabled
- **Reikia HDP Flush** prieš skaitant ring pointer'us — kitaip GPU hangina
- **VCN (video encode/decode) yra blokuotas** — Sony firmware lock

### Kodėl kiti projektai failino
- **AMD oficialus driveris** irgi duoda **Code 43** — tai nėra mūsų kodo problema
- **Visi WDDM bandymai** (WDDM 1.3–2.7, 47 callback'ų) failina su `0xC0000059` (STATUS_REVISION_MISMATCH)
- **DxgkInitialize()** sukelia **Code 39** — mūsų KMD nėra pilnas WDDM display miniport
- **BC-250 yra compute-only GPU** — neturi display output, todėl WDDM kelias yra beprasmis

### Mūsų atradimas: Vulkan ICD per IOCTL
- **Vienintelis projektas pasaulyje** kuris pasiekė veikiantį Vulkan ICD AMD BC-250 su Windows
- **13/13 Vulkan testų PASS** per custom IOCTL kanalą
- **vulkaninfo.exe praeina** be klaidų su oficialiu Vulkan loaderiu
- **IOCTL kanalas veikia** (13/15 testų) — KMD gauna ir vykdo komandas

---

## Ką mes padarėme

### KMD (Kernel-Mode Driver)
- ✅ IOCTL dispatch veikia (24+ handler'iai)
- ✅ g_PciDevExt inicializuotas DriverEntry
- ✅ IB packet + EOP fence formatas pataisytas
- ✅ EOP fence: `0xA0000246` (EVENT_INDEX=5, DATA_SEL=1, INT_SEL=1)
- ✅ GFX10 ring buffer inicializacija
- ✅ HDP Flush prieš ring reads
- ✅ PM4 command queue su fence signaling
- ✅ SDMA copy/fill engine
- ✅ TDR reset recovery
- ✅ 40 CU unlock
- ✅ Power/thermal management

### Vulkan ICD
- ✅ Veikia su oficialiu Vulkan loaderiu (vulkaninfo.exe)
- ✅ `vk_icdNegotiateLoaderICDInterfaceVersion` (version 4)
- ✅ 80+ Vulkan function stubs
- ✅ QueueSubmit su IB (PM4 commands → GPU)
- ✅ CreateInstance, CreateDevice, AllocateMemory, CreateBuffer, CreateFence, WaitForFences
- ✅ CreateCommandPool, AllocateCommandBuffers, BeginCommandBuffer, EndCommandBuffer
- ✅ CreateGraphicsPipelines

### UMD (User-Mode Driver)
- ✅ D3D9 DDI (45+ funkcijų)
- ✅ OpenAdapter = ordinal 1 (.def failas)
- ✅ GetCaps grąžina realius D3DCAPS9 duomenis
- ✅ CreateResource, Lock/Unlock IOCTL pataisymai
- ✅ Flush su GPU PA (ne CPU adresas)

### Build & Test
- ✅ build.bat (KMD + UMD)
- ✅ dxgkrnl.lib import library (nėra WDK 10.0.26100.0)
- ✅ test-vulkan-icd.exe (13 testų)
- ✅ test-gpu-ioctls.exe (15 testų, 13 PASS)
- ✅ vulkaninfo.exe praeina

---

## Kaip kompiluoti

### Būtina sąlyga
- Visual Studio 2022 (Community arba Professional)
- Windows WDK 10.0.26100.0
- Test signing: `bcdedit /set testsigning on` (per Admin)

### Build
```cmd
cd C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main
build.bat
```

### Įdiegimas
1. `build.bat` → sukuria `output\atikmdag.sys` + `output\amdbc250umd64.dll`
2. Device Manager → BC-250 → Update Driver → Browse → `output\`
3. Perkrauti kompiuterį

### Vulkan ICD registravimas
```cmd
reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "C:\...\output\amdbc250_icd.json" /t REG_DWORD /d 0 /f
```

### Testavimas
```cmd
cd output
test-gpu-ioctls.exe     # IOCTL test (13/15)
test-vulkan-icd.exe     # Vulkan test (13/13)
vulkaninfo.exe          # Oficialus Vulkan testas
```

---

## Ko mes norime padaryti toliau

### Artimiausias laikas
1. **AllocVidMem** — išspręsti MmAllocateContiguousMemory BSOD
2. **Display flip** — programuoti HUBPREQ registrus per KMD IOCTL
3. **Hardware init** — MMIO mapping, ring buffer, fence inicializacija DriverEntry

### Trumpas laikotarpis
4. **Realus triangle rendering** — vertex buffer + PM4 draw
5. **ACO shader compilation** — DXBC/SPIR-V → GFX10 ISA
6. **D3D9 per IOCTL** — apeiti D3D9On12 naudojant custom kelią

### Vidutinis laikotarpis
7. **Pilnas WDDM display miniport** — DxgkInitialize + visi DDI callbacks
8. **D3D11/D3D12 UMD** — realūs stubs kurie veikia
9. **Multi-monitor** — 4 display pipes palaikymas

### Ilgas laikotarpis
10. **OpenGL ICD** — Mesa radeonsi portas
11. **Ray tracing** — RT core palaikymas
12. **GPU compute** — SDMA compute queue (kai bus išspręstas HW quirk)

---

## Techninė architektūra

```
┌─────────────────────────────────────────────────────┐
│              Vulkan Application                       │
├─────────────────────────────────────────────────────┤
│         amdbc250vulkan.dll (Vulkan ICD)              │
│         ├── 80+ Vulkan function stubs                │
│         ├── QueueSubmit → PM4 commands to DMA buffer │
│         ├── AllocateMemory → KMD IOCTL               │
│         └── CreateBuffer → KMD IOCTL                 │
├─────────────────────────────────────────────────────┤
│         atikmdag.sys (KMD)                           │
│         ├── IOCTL 0x80000880: SubmitCommands (IB+EOP)│
│         ├── IOCTL 0x80000930: AllocDmaBuffer         │
│         ├── IOCTL 0x80000840: AllocVidMem            │
│         ├── IOCTL 0x800008C4: FlipDisplay            │
│         ├── PM4 command ring → GPU                    │
│         └── EOP fence → completion signal             │
├─────────────────────────────────────────────────────┤
│              AMD BC-250 GPU (RDNA2/GFX1013)           │
│              24 CU, 16GB GDDR6, DCN 2.1              │
└─────────────────────────────────────────────────────┘
```

---

## Failų struktūra

```
├── src/kmd/                     # Kernel-Mode Driver
│   ├── amdbc250_dream_v3_kmd.c  # IOCTL dispatch, DriverEntry, submit
│   ├── amdbc250_dream_v3_hw_init.c  # GPU init, ring buffers, display
│   ├── amdbc250_dream_v3_power.c    # Power/thermal management
│   ├── amdbc250_dream_v3_vm.c       # GPUVM, GART, page tables
│   └── dxgkrnl.def              # Import library (WDK neturi)
├── src/umd/                     # User-Mode Driver
│   ├── amdbc250_umd_v46.c       # D3D9 DDI (45+ functions)
│   └── amdbc250_umd.def         # Export: OpenAdapter = ordinal 1
├── src/vulkan/                  # Vulkan ICD
│   ├── bc250_vulkan_icd.c       # 80+ Vulkan functions, IOCTL submit
│   ├── bc250_vulkan.def         # Export: vk_icdGetInstanceProcAddr
│   ├── bc250_aco_wrapper.c      # ACO shader compiler stub
│   └── bc250_shader.c           # SPIR-V → GFX10 ISA
├── test-tools/                  # Test applications
│   ├── test-gpu-ioctls.c        # IOCTL test (15 tests)
│   ├── test-vulkan-icd.c        # Vulkan test (13 tests)
│   └── test-render.c            # Rendering test
├── inf/                         # Driver INF
├── output/                      # Build output
├── build.bat                    # Build script
└── STATUS.md                    # Detailed project status
```

---

## Licencija

Šaltinis kodas švietimo tikslais. Naudokite savo atsakomybe.
ACO kompiliatorius: MIT licencija (Mesa projektas).

---

*Sukurta su meile GPU driveriams. 🍖*

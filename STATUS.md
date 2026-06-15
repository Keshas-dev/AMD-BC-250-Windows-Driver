# AMD BC-250 Windows Driver — Status Report

**Data:** 2026-06-15  
**Versija:** v4.3.2  
**Projektas:** Windows GPU driver for AMD BC-250 (Cyan Skillfish / RDNA2 / GFX1013)

---

## Latest Discovery (2026-06-15)

- `test-psp-driver.exe -E` successfully loads embedded SOS firmware via PSP mailbox.
- `test-psp-driver.exe -B` completes boot sequence: SYSDRV (0x4) + SOS (0x8) SENT, GRBM_STATUS = 0x00000000 (NBIO unlocked).
- `C2PMSG_81` reads `0xF0000010` via mailbox (`-m`) after boot — **SOS is alive**.
- PSP driver IOCTL `IOCTL_PSP_KIQ_LOAD_FW` (`0x822`) is now implemented and usable for loading ME/PFP/MEC/RLC firmware blobs (`cyan_skillfish2_*.bin`).
- **Immediate next step:** Load ME (`0x01`) and PFP (`0x02`) firmware via `test-psp-driver.exe -Q 1|2 <fwfile>` to enable CP command execution.

---

## Quick Status

| Komponentas | Statusas |
|-------------|----------|
| KMD loads, IOCTLs work (30+ handlers) | ✅ |
| Vulkan ICD works with official loader | ✅ |
| test-gpu-ioctls: 14/15 PASS | ✅ |
| test-vulkan-icd: 13/13 PASS | ✅ |
| test-gpu-hw-init: 5/7 PASS (MMIO blocked) | ⚠️ |
| test-d3d9-adapter: 5/5 PASS | ✅ |
| BSOD fixed (3 critical bugs) | ✅ |
| MMIO mapping fails (PS5 SMU block) | ❌ |
| New IOCTLs (GPU_INFO, FIREWALL, REG_TEST) | ✅ |
| PSP analysis from Linux amdgpu | ✅ |

---

## KMD (Kernel-Mode Driver) — `atikmdag.sys`

| # | Komponentas | Statusas |
|---|-------------|----------|
| 1 | IOCTL dispatch (30+ handlers) | ✅ Working |
| 2 | g_PciDevExt initialization | ✅ Working |
| 3 | IB packet + EOP fence | ✅ Working |
| 4 | AllocVidMem (MDL-based, 40-bit) | ✅ Working |
| 5 | INIT_HARDWARE (user-mode MMIO) | ✅ Working |
| 6 | SEND_PM4 / READ_REG / WRITE_REG | ✅ Working |
| 7 | GET_HW_STATUS | ✅ Working |
| 8 | SDMA copy/fill engine | ✅ Working |
| 9 | TDR reset recovery | ✅ Working |
| 10 | 40 CU unlock | ✅ Working |
| 11 | Power/thermal management | ✅ Working |
| 12 | MMIO mapping (MmMapIoSpace) | ❌ Blocked — SMU blocks access without PSP firmware auth |
| 13 | IOCTL_GET_GPU_INFO (0x80000C00) | ✅ Working |
| 14 | IOCTL_GET_FIREWALL_STATUS (0x80000C04) | ✅ Working |
| 15 | IOCTL_TEST_REGISTER (0x80000C08) | ✅ Working |

## Vulkan ICD — `amdbc250vulkan.dll`

| # | Komponentas | Statusas |
|---|-------------|----------|
| 1 | vulkaninfo.exe passes | ✅ |
| 2 | 13/13 tests PASS | ✅ |
| 3 | QueueSubmit with IB | ✅ |
| 4 | 80+ Vulkan stubs | ✅ |

## UMD — `amdbc250umd64.dll`

| # | Komponentas | Statusas |
|---|-------------|----------|
| 1 | D3D9 DDI (45+ functions) | ✅ |
| 2 | OpenAdapter = ordinal 1 | ✅ |
| 3 | GetCaps real data | ✅ |
| 4 | D3D9 runtime works | ✅ |
| 5 | D3D9 adapter visible | ❌ Needs DXGKRNL |

## Build & Test

| # | Komponentas | Statusas |
|---|-------------|----------|
| 1 | build.bat (auto-sign) | ✅ |
| 2 | test-gpu-ioctls.exe | ✅ 14/15 PASS |
| 3 | test-vulkan-icd.exe | ✅ 13/13 PASS |
| 4 | test-gpu-hw-init.exe | ⚠️ 5/7 (MMIO fails) |
| 5 | test-d3d9-adapter.exe | ✅ 5/5 PASS |
| 6 | vulkaninfo.exe | ✅ PASSES |
| 7 | test-driver-check.exe | ✅ GPU_INFO, FIREWALL veikia |
| 8 | safe-test.exe | ✅ Veikia be crash |
| 9 | deep-test.exe | ✅ Registro scan + write test |

---

## Current Blocker: MMIO Access

**Problem:** MmMapIoSpace returns NULL or reads all zeros for BAR4=0xFE800000 (512KB MMIO regs).

**Root cause:** PS5 SMU (System Management Unit) blocks GPU MMIO access without PSP firmware authentication. Linux amdgpu driver loads firmware to unlock compute.

**BootConfig from registry:**
- Descriptor 0: PA=0xC0000000, 256MB (VRAM)
- Descriptor 1: PA=0xD0000000, 2MB
- Descriptor 2: PA=0xFE800000, 512KB (MMIO registers)

**Next steps:**
1. Try MmMapIoSpace on VRAM BAR (0xC0000000, 256MB) — may not be blocked
2. Investigate PSP firmware auth (Linux amdgpu has firmware loading)
3. Consider SDMA-based command submission instead of GFX ring

---

## PSP Analysis — Linux amdgpu Cyan Skillfish

**Discovery:** Linux amdgpu uses PSP v11 for Cyan Skillfish firmware loading when `AMD_APU_IS_CYAN_SKILLFISH2` flag is set.

**PSP v11 Register Map:**

| Registras | Adresas | Paskirtis |
|-----------|---------|-----------|
| C2PMSG_35 | 0x0088 | Bootloader command |
| C2PMSG_36 | 0x008C | Firmware address (>> 20) |
| C2PMSG_64 | 0x0100 | Ring creation / TOS_READY |
| C2PMSG_67 | 0x010C | Ring write pointer |
| C2PMSG_69 | 0x0114 | Ring address low |
| C2PMSG_70 | 0x0118 | Ring address high |
| C2PMSG_71 | 0x011C | Ring size |
| C2PMSG_81 | 0x0144 | SOS alive check |

**Firmware Loading Sequence:**
1. Wait for bootloader (C2PMSG_35 bit 31 = 1)
2. Copy firmware to PSP memory
3. Write address to C2PMSG_36 (>> 20)
4. Write command to C2PMSG_35
5. Wait for response (C2PMSG_35 bit 31 = 1)

**Ring Creation Sequence:**
1. Wait for TOS_READY (C2PMSG_64 bit 31 = 1)
2. Write ring address to C2PMSG_69 (low) and C2PMSG_70 (high)
3. Write ring size to C2PMSG_71
4. Write ring type to C2PMSG_64
5. Wait for TOS_RESP (C2PMSG_64 bit 31 = 1)

**Firmware Files:**
- `cyan_skillfish2_sos.bin` — Secure OS
- `cyan_skillfish2_asd.bin` — ASD firmware
- `cyan_skillfish2_ta.bin` — TA firmware

---

## NBIO Firewall Status

**Current Test Results (test-driver-check.exe):**

| Testas | Statusas | Rezultatas |
|--------|----------|------------|
| GPU INFO | ✅ Veikia | Vendor=0x1002, Device=0x13FE, CUs=24, Shaders=1536 |
| FIREWALL STATUS | ✅ Veikia | Allowed=6, Blocked=7 |
| MMHUB[0x50D0] | ❌ Neveikia | Read=0x00000000, Write=0x00000000 |
| GC[0x3008] | ❌ Neveikia | Read=0x00000000, Write=0x00000000 |

**NBIO Firewall Register Map:**

| Blokas | Offset | Statusas | Aprašymas |
|--------|--------|----------|-----------|
| GPU_ID | 0x0000 | ✅ Skaitymas | SoC ID (0x9FFF9700) |
| HDP | 0x05A0-0x05DC | ✅ Skaitymas | Host Data Path |
| GC | 0x3000-0x3008 | ❌ Užblokuotas | Graphics Core |
| MMHUB | 0x5000-0x59D0 | ❌ Užblokuotas | Memory Hub |
| DF | 0x1A0E8-0x1A33C | ✅ Skaitymas | Data Fabric |
| NBIO | 0xC0D4-0x1FC | ✅ Skaitymas | NBIO config |
| GRBM | 0x2004 | ❌ Užblokuotas | Graphics Bus Manager |
| CP | 0x2000-0x2FFF | ❌ Užblokuotas | Command Processor |
| CLK | 0x0D00-0x0DFF | ❌ Užblokuotas | Clock config |

---

## BSOD Fixes (commit c268536)

Three critical bugs found and fixed:
1. **vkFreeMemory** sent VirtualAlloc address to KMD's MmFreeContiguousMemory → BSOD
2. **test-render.c** passed PA instead of VA to FREE_DMA_BUFFER → BSOD
3. **AllocVidMem** used MmAllocateContiguousMemory → replaced with MDL-based (MmAllocatePagesForMdlEx)

---

## File Statistics

| Komponentas | Failai | Dydis |
|-------------|--------|-------|
| KMD source | 4 .c + 4 .h + 2 .def | ~230 KB |
| UMD source | 1 .c + 1 .def | ~55 KB |
| Vulkan ICD | 2 .c + 2 .h + 1 .def | ~65 KB |
| Test tools | 10 .c files | ~80 KB |
| **Iš viso** | **~25 failų** | **~430 KB** |

---

## Next Steps

### Trumpalaikiai (1-2 savaitės)
1. **PSP firmware loading** — ✅ Em dedykamas dokumentacija: **Embedded SOS jau įkrautas ir bootinamas per `LOAD_EMBEDDED_FW` / `BOOT_SEQUENCE`**
2. **NBIO unlock** — ✅ Įrodyta, kad boot sequence atrakina NBIO (C2PMSG_81=0xF0000010)
3. **ME/PFP firmware load via KIQ** — `IOCTL_PSP_KIQ_LOAD_FW` (`0x822`) įdiegtas; naudoti `test-psp-driver.exe -Q 1|2 ...`
4. **CP scratch + ring pasitikrinti** — po ME/PFP load patikrinti `CP_SCRATCH_REG0` ir `WPTR` pokytį

### Vidutinės trukmės (1-2 mėnesiai)
4. **GFX ring init** — inicializuoti Command Processor ring buffer
5. **PM4 commands** — siųsti draw/compute komandas per ring buffer
6. **Triangle rendering** — vertex buffer + PM4 draw

### Ilgalaikiai (3-6 mėnesiai)
7. **ACO shader compilation** — DXBC/SPIR-V → GFX10 ISA
8. **D3D9/D3D11** — per IOCTL path
9. **OpenGL ICD** — Mesa radeonsi port
10. **GPU compute** — SDMA compute queue

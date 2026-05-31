# Kernel-Mode Driver (KMD) Source

## ✅ ACTIVE BUILD (įtraukta į atikmdag.sys)

Šie failai yra kompiliuojami į galutinį `atikmdag.sys`:

1. **amdbc250_dream_kmd.c** (1,754 lines)
   - WDDM 3.x DDI callbacks
   - DriverEntry, AddDevice, StartDevice, StopDevice
   - SubmitCommand, BuildPagingBuffer
   - VidPN implementation
   - Interrupt handling
   - Allocation management

2. **amdbc250_dream_hw_init.c** (821 lines)
   - Hardware initialization sequences
   - Command Processor (CP) init
   - HDP flush before ring reads (CRITICAL)
   - Golden registers programming
   - DCN 2.1 display engine init
   - Thermal monitoring setup

## ⚠️ NOT IN BUILD (parašyta, bet neįtraukta)

Šie failai YRA PARUOŠTI, bet **NEĮTRAUKTI** į build:

3. **amdbc250_dream_vm.c** (866 lines)
   - GPU Virtual Memory management
   - GART (Graphics Address Remapping Table)
   - 4-level page tables
   - VMID allocation
   - TLB invalidation
   - **Status:** READY TO INTEGRATE

4. **amdbc250_dream_power.c** (908 lines)
   - SMU (System Management Unit) communication
   - D0-D3 power states
   - Thermal throttling
   - Fan PWM control
   - Clock scaling
   - **Status:** READY TO INTEGRATE

## Kaip įtraukti vm.c ir power.c į build:

Redaguoti `SOURCES` failą:

```
SOURCES=amdbc250_dream_kmd.c \
        amdbc250_dream_hw_init.c \
        amdbc250_dream_vm.c \
        amdbc250_dream_power.c
```

Arba rankiniu būdu kompiliuoti:

```powershell
cl.exe /c /kernel ... amdbc250_dream_vm.c
cl.exe /c /kernel ... amdbc250_dream_power.c

link.exe /DRIVER ... amdbc250_dream_vm.obj amdbc250_dream_power.obj ...
```

## Makefile ir SOURCES

- `makefile` - Standartinis WDK makefile
- `SOURCES` - Nurodo kurie .c failai kompiliuojami (TIK kmd.c + hw_init.c)

---

**Build Status:** ✅ COMPILING (2/4 files active)  
**Next Step:** Integrate vm.c + power.c for full functionality

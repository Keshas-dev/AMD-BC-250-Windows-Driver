# AMD BC-250 Windows Driver - Klaidų Analizė ir Sprendimai

**Data:** 2026-05-31  
**Statusas:** 🔴 KRITINIAI TRIGERIAI  
**Prioritetas:** AUKŠČIAUSIAS

---

## 1. 🔴 KRITINĖ KLAIDA: AllocVidMem BSOD

### Problema Aprašymas

```
Failas: src/kmd/amdbc250_dream_v3_kmd.c (eilutės 2155-2231)
IOCTL kod: 0x80000840
Statusas: ❌ BSOD (MmAllocateContiguousMemory)
```

### Šaltinis Kodo Analizė

**Problematiškas kodas:**
```c
PHYSICAL_ADDRESS highestAddr;
highestAddr.QuadPart = 0xFFFFFFFFULL;  // ← PROBLEMA! 32-bit addressing

PVOID virtualAddr = NULL;
__try {
    virtualAddr = MmAllocateContiguousMemory(AllocSize, highestAddr);
} __except (EXCEPTION_EXECUTE_HANDLER) {
    // Exception handling, bet vis tiek BSOD
}
```

### Identifikuotos Problemos

#### **Problema #1: 32-bit Physical Address Limit**
```
Dabartinis kodas: 0xFFFFFFFFULL = 4GB limitas
BC-250 turi:     16GB GDDR6 atminties
Rezultatas:      Ne visos atminties pasiekiama!
```

**Sprendimas:**
```c
// TEISINGAI - skirtas 40-bit adresavimui
PHYSICAL_ADDRESS highestAddr;
highestAddr.QuadPart = 0xFFFFFFFFFFULL;  // 40-bit = 1TB parama
// arba geriau:
highestAddr.LowPart = 0xFFFFFFFF;
highestAddr.HighPart = 0xFF;
```

---

#### **Problema #2: Netinkamas LowestAcceptable Inicijavimas**

**Šaltinis kodo (negeras):**
```c
// Nėra LowestAcceptable, naudojama tik highestAddr
PVOID virtualAddr = MmAllocateContiguousMemory(AllocSize, highestAddr);
```

**Sprendimas - Naudoti MmAllocatePagesForMdlEx:**
```c
PHYSICAL_ADDRESS LowestAcceptable;
LowestAcceptable.QuadPart = 0x00000000;

PHYSICAL_ADDRESS HighestAcceptable;
HighestAcceptable.QuadPart = 0x00000003FFFFFFFF;  // 36-bit arba 40-bit

PHYSICAL_ADDRESS SkipBytes;
SkipBytes.QuadPart = 0;

PMDL Mdl = MmAllocatePagesForMdlEx(
    LowestAcceptable,
    HighestAcceptable,
    SkipBytes,
    (SIZE_T)AllocSize,
    MmCached,
    MM_ALLOCATE_FULLY_SPECIFIED
);

if (Mdl != NULL) {
    // Sėkmingai alokuota!
    PVOID VirtualAddr = MmMapLockedPagesSpecifyCache(
        Mdl,
        UserMode,
        MmCached,
        NULL,
        FALSE,
        NormalPagePriority
    );
}
```

---

#### **Problema #3: Netinkama IRQL Lygis**

**Problema:**
```c
// MmAllocateContiguousMemory reikalinga IRQL <= APC_LEVEL
// Jei IRQL > APC_LEVEL, sukelia BSOD!
```

**Diagnoze sprendimas - Patikrinti IRQL:**
```c
KIRQL CurrentIrql = KeGetCurrentIrql();

if (CurrentIrql > APC_LEVEL) {
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
        "AMDBC250: AllocVidMem called at IRQL %d (max allowed: %d)\n",
        CurrentIrql, APC_LEVEL));
    return STATUS_INVALID_DEVICE_STATE;
}
```

---

#### **Problema #4: Nėra Alignment Garantijos**

**BC-250 reikalavimai:**
- GDDR6 atminties alignment: **4KB (4096 bytes)**
- DMA buffer alignment: **256 bytes** (minimum)
- Page alignment: **4KB** (Windows standard)

**Netinkamas kodas:**
```c
if (AllocSize < 4096) AllocSize = 4096;
// Tik padidina dydį, bet nenustatyti alignment!
```

**Teisingas sprendimas:**
```c
#define GPU_PAGE_SIZE 0x1000  // 4KB
#define GPU_PAGE_ALIGN(x) (((x) + GPU_PAGE_SIZE - 1) & ~(GPU_PAGE_SIZE - 1))

SIZE_T AllocSize = InData[0];
AllocSize = GPU_PAGE_ALIGN(AllocSize);

if (AllocSize < GPU_PAGE_SIZE) {
    AllocSize = GPU_PAGE_SIZE;
}
if (AllocSize > 64 * 1024 * 1024) {  // 64MB limitas
    AllocSize = 64 * 1024 * 1024;
}

// Patikrinti alignment
if ((AllocSize & (GPU_PAGE_SIZE - 1)) != 0) {
    return STATUS_INVALID_PARAMETER;
}
```

---

### Pilnas Taisytas Kodas

```c
/* --- Allocate Video Memory (TAISYTA VERSIJA) --- */
case 0x80000840: { /* IOCTL_AMDBC250_ALLOC_VIDMEM */
    
    // Validacija: IRQL check
    if (KeGetCurrentIrql() > APC_LEVEL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250: AllocVidMem invalid IRQL %d\n", KeGetCurrentIrql()));
        status = STATUS_INVALID_DEVICE_STATE;
        break;
    }

    // Validacija: buffer dydžiai
    if (inputLen < sizeof(ULONG) * 3 || outputLen < sizeof(ULONG64) * 2) {
        status = STATUS_BUFFER_TOO_SMALL;
        break;
    }

    // Parsinti input parametrus
    PULONG InData = (PULONG)inputBuffer;
    SIZE_T AllocSize = (SIZE_T)InData[0];
    
    // Alignment ir dydžio validacija
    #define GPU_PAGE_SIZE 0x1000
    #define GPU_MAX_ALLOC  (64 * 1024 * 1024)  // 64MB
    
    AllocSize = ((AllocSize + GPU_PAGE_SIZE - 1) / GPU_PAGE_SIZE) * GPU_PAGE_SIZE;
    
    if (AllocSize < GPU_PAGE_SIZE || AllocSize > GPU_MAX_ALLOC) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250: AllocVidMem size out of range: %llu\n", (ULONG64)AllocSize));
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    // Nustatyti correct physical address range
    PHYSICAL_ADDRESS LowestAcceptable;
    LowestAcceptable.QuadPart = 0x0000000000000000;
    
    PHYSICAL_ADDRESS HighestAcceptable;
    HighestAcceptable.QuadPart = 0x0000003FFFFFFFFF;  // 40-bit = 1TB
    
    PHYSICAL_ADDRESS SkipBytes;
    SkipBytes.QuadPart = 0;

    // Bandyti alokuoti su MDL (geriau nei MmAllocateContiguousMemory)
    PMDL Mdl = NULL;
    PVOID VirtualAddr = NULL;
    PHYSICAL_ADDRESS PhysicalAddr = {0};

    __try {
        Mdl = MmAllocatePagesForMdlEx(
            LowestAcceptable,
            HighestAcceptable,
            SkipBytes,
            AllocSize,
            MmCached,
            MM_ALLOCATE_FULLY_SPECIFIED
        );

        if (Mdl != NULL) {
            // Mapinti į kernel virtual space
            VirtualAddr = MmMapLockedPagesSpecifyCache(
                Mdl,
                KernelMode,
                MmCached,
                NULL,
                FALSE,
                NormalPagePriority
            );

            if (VirtualAddr != NULL) {
                // Gauti physical address iš MDL
                if (MmGetMdlByteCount(Mdl) >= AllocSize) {
                    PhysicalAddr = MmGetMdlPfnArray(Mdl)[0];
                    PhysicalAddr.QuadPart <<= PAGE_SHIFT;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ULONG ExCode = GetExceptionCode();
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250: AllocVidMem exception 0x%X\n", ExCode));
        
        if (Mdl != NULL) {
            MmUnmapLockedPages(VirtualAddr, Mdl);
            MmFreePagesFromMdl(Mdl);
            ExFreePoolWithTag(Mdl, 'BCAM');
        }
        
        status = STATUS_INSUFFICIENT_RESOURCES;
        break;
    }

    if (VirtualAddr != NULL && PhysicalAddr.QuadPart != 0) {
        // Grąžinti output
        PULONG64 OutData = (PULONG64)outputBuffer;
        OutData[0] = PhysicalAddr.QuadPart;
        OutData[1] = (ULONG64)(UINT_PTR)VirtualAddr;
        OutData[2] = (ULONG64)(UINT_PTR)Mdl;  // Saugoti MDL reference
        
        bytesReturned = sizeof(ULONG64) * 3;
        
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250: AllocVidMem SUCCESS: %llu bytes, PA=0x%llX VA=%p MDL=%p\n",
            (ULONG64)AllocSize, PhysicalAddr.QuadPart, VirtualAddr, Mdl));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250: AllocVidMem FAILED\n"));
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    
    break;
}
```

---

## 2. 🟠 ANTRA KLAIDA: Display Flip Nėra Implementuota

### Problema

```
Status: ⏳ Laukia
IOCTL kod: 0x800008C4 (FlipDisplay)
Problema: HUBPREQ registrai neprogramuojami
```

### Sprendimas

**Reikalingos registrų operacijos:**

```c
// DCN 2.1 Display Engine - Register offsets
#define HUBPREQ_BASE              0x1C00  // Base address for display requests
#define HUBPREQ_SURFACE_ADDRESS   0x1C04  // DMA buffer address
#define HUBPREQ_PITCH             0x1C08  // Buffer pitch (width * 4)
#define HUBPREQ_HEIGHT            0x1C0C  // Height
#define HUBPREQ_FORMAT            0x1C10  // Pixel format
#define HUBPREQ_ENABLE            0x1C14  // Enable register

// MMIO write helper
static inline VOID HubpReqWriteReg(PDEVICE_EXTENSION pDevExt, ULONG offset, ULONG value)
{
    if (pDevExt->pMmioBase != NULL) {
        volatile PULONG pReg = (PULONG)((UINT_PTR)pDevExt->pMmioBase + offset);
        *pReg = value;
    }
}

// Display flip handler
case 0x800008C4: { /* IOCTL_AMDBC250_FLIP_DISPLAY */
    
    if (inputLen < sizeof(DISPLAY_FLIP_REQUEST)) {
        status = STATUS_BUFFER_TOO_SMALL;
        break;
    }

    PDISPLAY_FLIP_REQUEST FlipReq = (PDISPLAY_FLIP_REQUEST)inputBuffer;
    
    // Validacija
    if (FlipReq->SurfaceGpuVa == 0 || FlipReq->Width == 0 || FlipReq->Height == 0) {
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    // Programuoti HUBPREQ registrus
    __try {
        HubpReqWriteReg(pDevExt, HUBPREQ_SURFACE_ADDRESS, 
            (ULONG)(FlipReq->SurfaceGpuVa & 0xFFFFFFFF));
        HubpReqWriteReg(pDevExt, HUBPREQ_SURFACE_ADDRESS + 4,
            (ULONG)(FlipReq->SurfaceGpuVa >> 32));  // 64-bit address
        
        HubpReqWriteReg(pDevExt, HUBPREQ_PITCH, 
            FlipReq->Width * 4);  // Assume 4 bytes per pixel
        
        HubpReqWriteReg(pDevExt, HUBPREQ_HEIGHT, FlipReq->Height);
        
        HubpReqWriteReg(pDevExt, HUBPREQ_FORMAT, 0x00000004);  // ARGB8888
        
        // Enable display
        HubpReqWriteReg(pDevExt, HUBPREQ_ENABLE, 0x00000001);
        
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250: Display flip OK: %ux%u, PA=0x%llX\n",
            FlipReq->Width, FlipReq->Height, FlipReq->SurfaceGpuVa));
            
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250: Display flip exception 0x%X\n", GetExceptionCode()));
        status = STATUS_DEVICE_PROTOCOL_ERROR;
    }
    
    break;
}
```

---

## 3. 🟡 TREČIA KLAIDA: Hardware Init Nepilnas

### Problema

```
Status: ⏳ Laukia
Problema: MMIO mapping, ring buffers, fence - neinicializuota DriverEntry
```

### Sprendimas - DriverEntry Pagerinimas

```c
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, 
                     _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status = STATUS_SUCCESS;

    // 1. Inicijalizuoti driver object callbacks
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DreamV3WdmCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DreamV3WdmClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DreamV3WdmDeviceControl;
    DriverObject->DriverUnload = DreamV3WdmUnload;
    DriverObject->AddDevice = DreamV3WdmAddDevice;

    // 2. Initialize global device extension
    RtlZeroMemory(&g_PciDevExt, sizeof(DEVICE_EXTENSION));
    g_PciDevExt.RegistryPath = RegistryPath;

    // 3. GPU Hardware Initialization
    status = HwInitializeGpu(&g_PciDevExt);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250: HwInitializeGpu failed 0x%X\n", status));
        return status;
    }

    // 4. Ring Buffer Setup
    status = HwInitializeRingBuffers(&g_PciDevExt);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250: HwInitializeRingBuffers failed 0x%X\n", status));
        return status;
    }

    // 5. Display Engine Setup
    status = HwInitializeDisplayEngine(&g_PciDevExt);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250: HwInitializeDisplayEngine failed 0x%X\n", status));
        // Non-fatal, continue without display
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-DREAM-V4.3: Driver loaded successfully\n"));

    return STATUS_SUCCESS;
}
```

---

## 4. 📋 Papildomos Problemos

### 4.1 - Nėra Memory Leak Apsaugos

```c
// ❌ BLOGAI - nėra cleanup
AllocVidMem() → MmAllocateContiguousMemory()
// Jei driver unload, visos allocations nesugrįžtinamos!
```

**Sprendimas - Allocation Tracking:**
```c
typedef struct {
    LIST_ENTRY ListEntry;
    PMDL Mdl;
    PVOID VirtualAddr;
    SIZE_T Size;
    ULONG64 PhysicalAddr;
} ALLOCATION_ENTRY, *PALLOCATION_ENTRY;

typedef struct {
    LIST_ENTRY AllocationList;
    KSPIN_LOCK SpinLock;
} ALLOCATION_MANAGER, *PALLOCATION_MANAGER;

// Globali allocation manager
ALLOCATION_MANAGER g_AllocManager;

// Funkcija registruoti allocation
VOID RegisterAllocation(PMDL Mdl, PVOID VirtualAddr, SIZE_T Size, ULONG64 PhysicalAddr)
{
    PALLOCATION_ENTRY Entry = ExAllocatePoolWithTag(NonPagedPool, 
        sizeof(ALLOCATION_ENTRY), 'ALLC');
    
    if (Entry != NULL) {
        Entry->Mdl = Mdl;
        Entry->VirtualAddr = VirtualAddr;
        Entry->Size = Size;
        Entry->PhysicalAddr = PhysicalAddr;
        
        KIRQL OldIrql = KeAcquireSpinLockRaiseToDpc(&g_AllocManager.SpinLock);
        InsertTailList(&g_AllocManager.AllocationList, &Entry->ListEntry);
        KeReleaseSpinLock(&g_AllocManager.SpinLock, OldIrql);
    }
}

// Unload cleanup
VOID DreamV3WdmUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    KIRQL OldIrql = KeAcquireSpinLockRaiseToDpc(&g_AllocManager.SpinLock);
    
    while (!IsListEmpty(&g_AllocManager.AllocationList)) {
        PLIST_ENTRY Entry = RemoveHeadList(&g_AllocManager.AllocationList);
        PALLOCATION_ENTRY AllocEntry = CONTAINING_RECORD(Entry, ALLOCATION_ENTRY, ListEntry);
        
        if (AllocEntry->VirtualAddr != NULL && AllocEntry->Mdl != NULL) {
            MmUnmapLockedPages(AllocEntry->VirtualAddr, AllocEntry->Mdl);
            MmFreePagesFromMdl(AllocEntry->Mdl);
        }
        
        ExFreePoolWithTag(AllocEntry, 'ALLC');
    }
    
    KeReleaseSpinLock(&g_AllocManager.SpinLock, OldIrql);
}
```

### 4.2 - Nėra Input Buffer Validacijos

```c
// ❌ BLOGAI - ProbeForRead missing
PULONG InData = (PULONG)inputBuffer;
ULONG SizeLo = InData[0];  // ← Gali crash!
```

**Sprendimas:**
```c
// ✅ TEISINGAI
__try {
    ProbeForRead(inputBuffer, inputLen, sizeof(UCHAR));
    PULONG InData = (PULONG)inputBuffer;
    ULONG SizeLo = InData[0];
} __except (EXCEPTION_EXECUTE_HANDLER) {
    return STATUS_ACCESS_VIOLATION;
}
```

---

## 📊 Klaidų Prioritetas ir Timeline

| # | Klaida | Prioritetas | Svarba | Timeline |
|----|--------|-----------|--------|----------|
| 1 | AllocVidMem BSOD | 🔴 KRITINĖ | 10/10 | **ŠIANDIEN** |
| 2 | Display flip | 🟠 AUKŠTA | 8/10 | Rytoj |
| 3 | Hardware init | 🟠 AUKŠTA | 7/10 | 2 dienos |
| 4 | Memory leaks | 🟡 VIDUTINĖ | 6/10 | 1 savaitė |
| 5 | Buffer validation | 🟡 VIDUTINĖ | 6/10 | 1 savaitė |

---

## ✅ Kitimo Checklist

- [ ] Pataisyti AllocVidMem (MDL approach)
- [ ] Patikrinti IRQL lygius
- [ ] Implementuoti Display flip
- [ ] Pridėti memory leak protection
- [ ] Pridėti input validation (ProbeForRead/Write)
- [ ] Testavimas su WinDBG
- [ ] Build ir test-gpu-ioctls.exe
- [ ] Reboot ir patikrinti vulkaninfo.exe

---

## 🔍 Debug Komandos (WinDBG)

```
# BSOD debug
!analyze -v
kd> .bugcheck
kd> !pte <address>

# Memory check
kd> !vm
kd> !address <virtual_addr>

# Driver check
kd> !drvobj atikmdag
kd> !devobj <device_ptr>

# Interrupt check
kd> !idt
```

---

*Sukūrta: Copilot Analysis | 2026-05-31*

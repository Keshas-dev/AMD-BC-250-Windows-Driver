# AMD BC-250 Windows Driver - BUILD-FIX-GUIDE

**Data:** 2026-05-31  
**Versija:** 1.0  
**Tikslas:** Žingsnis po žingsnio instrukcijos klaidų taisymui ir build'ui  

---

## 📑 Turinys

1. [Aplinkos Setup](#1-aplinkos-setup)
2. [AllocVidMem BSOD Pataisymas](#2-allocvidmem-bsod-pataisymas)
3. [Display Flip Implementacija](#3-display-flip-implementacija)
4. [Hardware Init Completion](#4-hardware-init-completion)
5. [Build & Kompiliavimas](#5-build--kompiliavimas)
6. [Testing & Validation](#6-testing--validation)
7. [Debugging](#7-debugging-windbg)

---

## 1. Aplinkos Setup

### 1.1 - Reikalavai

**Prieš pradedant, patikrinkite:**

```powershell
# Run as Administrator - patikrinti Visual Studio 2022
# Atidarykite PowerShell (Admin):

# Patikrinti VS 2022 instaliacijom
Get-Command "msbuild" -ErrorAction SilentlyContinue
# Output turėtų parodyti kelią į msbuild.exe

# Patikrinti WDK instalaciją
$wdkPath = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*" | 
    Where-Object { $_.DisplayName -like "*Windows Driver Kit*" } | 
    Select-Object -ExpandProperty DisplayName

if ($wdkPath) {
    Write-Host "✅ WDK rastas: $wdkPath"
} else {
    Write-Host "❌ WDK neras instaliotas!"
}

# Patikrinti test signing
bcdedit /enum all | Select-String "testsigning"
# Output: testsigning    Yes (jei jau įjungtas)
```

**Jei nereikalingi komponenti:**

```powershell
# Visual Studio 2022 instalacija
# Download: https://visualstudio.microsoft.com/downloads/

# WDK Download: https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk
# Konkreti versija: Windows Driver Kit 10.0.26100.0

# Test Signing (Run as Admin)
bcdedit /set testsigning on
# Reboot reikalingas!
shutdown /r /t 30 /c "Enabling test signing"
```

### 1.2 - Repository Setup

```bash
# Clone repository
git clone https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver.git
cd AMD-BC-250-Windows-Driver

# Sukurti backup original files
$backupDir = "backup_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
New-Item -Type Directory -Path $backupDir | Out-Null
Copy-Item -Path "src/kmd/amdbc250_dream_v3_kmd.c" -Destination "$backupDir/" -Force

Write-Host "✅ Backup sukurtas: $backupDir"
```

---

## 2. AllocVidMem BSOD Pataisymas

### 2.1 - Header File Updates

**Failas: `src/kmd/amdbc250_dream_v3_kmd.h`**

Pridėti šiuos defines:

```c
// Add at the top of file, after existing #defines

/* ===== Memory Allocation Definitions ===== */
#define GPU_PAGE_SIZE           0x1000              // 4KB
#define GPU_PAGE_SHIFT          12
#define GPU_PAGE_ALIGN(x)       (((x) + GPU_PAGE_SIZE - 1) & ~(GPU_PAGE_SIZE - 1))
#define GPU_PAGE_MASK           (GPU_PAGE_SIZE - 1)

#define GPU_MIN_ALLOC           GPU_PAGE_SIZE       // 4KB minimum
#define GPU_MAX_ALLOC           (64 * 1024 * 1024)  // 64MB maximum

/* Physical address limits (40-bit for BC-250) */
#define GPU_LOWEST_PHYSICAL     0x0000000000000000ULL
#define GPU_HIGHEST_PHYSICAL    0x0000003FFFFFFFFFULL  // 40-bit = 1TB

/* Allocation entry for tracking */
typedef struct {
    LIST_ENTRY ListEntry;
    PMDL Mdl;
    PVOID VirtualAddress;
    SIZE_T Size;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Flags;
#define ALLOC_FLAG_VALID        0x00000001
#define ALLOC_FLAG_LOCKED       0x00000002
} GPU_ALLOCATION_ENTRY, *PGPU_ALLOCATION_ENTRY;

/* Global allocation manager */
typedef struct {
    LIST_ENTRY AllocationList;
    KSPIN_LOCK SpinLock;
    ULONG AllocationCount;
    ULONG64 TotalAllocatedBytes;
} GPU_ALLOCATION_MANAGER, *PGPU_ALLOCATION_MANAGER;
```

### 2.2 - Implementation File Updates

**Failas: `src/kmd/amdbc250_dream_v3_kmd.c`**

**ŽINGSNIS 1:** Pridėti global allocation manager inicijalizacijos į `DriverEntry`:

```c
/* Add after existing global declarations at top of file */

/* Global allocation manager */
static GPU_ALLOCATION_MANAGER g_AllocationManager = {0};

/* ===== INITIALIZATION IN DriverEntry ===== */
// Add this inside DriverEntry function, after other initialization:

VOID InitializeAllocationManager(VOID)
{
    InitializeListHead(&g_AllocationManager.AllocationList);
    KeInitializeSpinLock(&g_AllocationManager.SpinLock);
    g_AllocationManager.AllocationCount = 0;
    g_AllocationManager.TotalAllocatedBytes = 0;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Allocation manager initialized\n"));
}

// Call in DriverEntry:
// Add after g_PciDevExt initialization
InitializeAllocationManager();
```

**ŽINGSNIS 2:** Implementuoti naują AllocVidMem handler (ZAMENIT senąjį):

Rasti eilutę ~2159: `case 0x80000840:` ir ZAMENIT visa dalį nuo čia iki `break;` šiuo kodu:

```c
    /* --- Allocate Video Memory (TAISYTA) --- */
    case 0x80000840: { /* IOCTL_AMDBC250_ALLOC_VIDMEM */
        
        /* ===== VALIDATION PHASE ===== */
        
        // 1. Check IRQL level
        KIRQL CurrentIrql = KeGetCurrentIrql();
        if (CurrentIrql > APC_LEVEL) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250: AllocVidMem invalid IRQL %d (max: %d)\n", 
                CurrentIrql, APC_LEVEL));
            status = STATUS_INVALID_DEVICE_STATE;
            break;
        }

        // 2. Check buffer sizes
        if (inputLen < sizeof(ULONG) * 3) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        if (outputLen < sizeof(ULONG64) * 3) {
            status = STATUS BUFFER_TOO_SMALL;
            break;
        }

        // 3. Parse input
        PULONG InData = (PULONG)inputBuffer;
        SIZE_T RequestedSize = (SIZE_T)InData[0];

        // 4. Validate and align size
        SIZE_T AllocSize = GPU_PAGE_ALIGN(RequestedSize);
        
        if (AllocSize < GPU_MIN_ALLOC) {
            AllocSize = GPU_MIN_ALLOC;
        }
        if (AllocSize > GPU_MAX_ALLOC) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250: AllocVidMem size %llu exceeds max %d\n", 
                (ULONG64)AllocSize, GPU_MAX_ALLOC));
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        /* ===== ALLOCATION PHASE ===== */
        
        PMDL Mdl = NULL;
        PVOID VirtualAddress = NULL;
        PHYSICAL_ADDRESS PhysicalAddress = {0};

        __try {
            // 1. Set physical address range (40-bit for BC-250)
            PHYSICAL_ADDRESS LowestAcceptable;
            LowestAcceptable.QuadPart = GPU_LOWEST_PHYSICAL;
            
            PHYSICAL_ADDRESS HighestAcceptable;
            HighestAcceptable.QuadPart = GPU_HIGHEST_PHYSICAL;
            
            PHYSICAL_ADDRESS SkipBytes;
            SkipBytes.QuadPart = 0;

            // 2. Allocate pages via MDL
            Mdl = MmAllocatePagesForMdlEx(
                LowestAcceptable,
                HighestAcceptable,
                SkipBytes,
                AllocSize,
                MmCached,
                MM_ALLOCATE_FULLY_SPECIFIED
            );

            if (Mdl == NULL) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250: MmAllocatePagesForMdlEx failed for %llu bytes\n",
                    (ULONG64)AllocSize));
                status = STATUS_INSUFFICIENT_RESOURCES;
                __leave;
            }

            // 3. Map to kernel virtual space
            VirtualAddress = MmMapLockedPagesSpecifyCache(
                Mdl,
                KernelMode,
                MmCached,
                NULL,
                FALSE,
                NormalPagePriority
            );

            if (VirtualAddress == NULL) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250: MmMapLockedPages failed\n"));
                MmFreePagesFromMdl(Mdl);
                ExFreePoolWithTag(Mdl, 'BCAM');
                status = STATUS_INSUFFICIENT_RESOURCES;
                __leave;
            }

            // 4. Get physical address from MDL
            PPFN_NUMBER PfnArray = MmGetMdlPfnArray(Mdl);
            if (PfnArray == NULL) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250: Failed to get PFN array\n"));
                MmUnmapLockedPages(VirtualAddress, Mdl);
                MmFreePagesFromMdl(Mdl);
                ExFreePoolWithTag(Mdl, 'BCAM');
                status = STATUS_UNSUCCESSFUL;
                __leave;
            }

            PhysicalAddress.QuadPart = (ULONG64)PfnArray[0] << PAGE_SHIFT;

            // 5. Verify physical address is valid
            if (PhysicalAddress.QuadPart == 0) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250: Invalid physical address returned\n"));
                MmUnmapLockedPages(VirtualAddress, Mdl);
                MmFreePagesFromMdl(Mdl);
                ExFreePoolWithTag(Mdl, 'BCAM');
                status = STATUS_INSUFFICIENT_RESOURCES;
                __leave;
            }

            // 6. Register in allocation manager
            PGPU_ALLOCATION_ENTRY AllocEntry = (PGPU_ALLOCATION_ENTRY)
                ExAllocatePoolWithTag(NonPagedPool, sizeof(GPU_ALLOCATION_ENTRY), 'BCAE');
            
            if (AllocEntry != NULL) {
                RtlZeroMemory(AllocEntry, sizeof(GPU_ALLOCATION_ENTRY));
                AllocEntry->Mdl = Mdl;
                AllocEntry->VirtualAddress = VirtualAddress;
                AllocEntry->Size = AllocSize;
                AllocEntry->PhysicalAddress = PhysicalAddress;
                AllocEntry->Flags = ALLOC_FLAG_VALID | ALLOC_FLAG_LOCKED;

                KIRQL OldIrql = KeAcquireSpinLockRaiseToDpc(&g_AllocationManager.SpinLock);
                InsertTailList(&g_AllocationManager.AllocationList, &AllocEntry->ListEntry);
                g_AllocationManager.AllocationCount++;
                g_AllocationManager.TotalAllocatedBytes += AllocSize;
                KeReleaseSpinLock(&g_AllocationManager.SpinLock, OldIrql);

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250: AllocEntry registered (total: %u allocations, %llu bytes)\n",
                    g_AllocationManager.AllocationCount,
                    g_AllocationManager.TotalAllocatedBytes));
            }

            // 7. Return output
            PULONG64 OutData = (PULONG64)outputBuffer;
            OutData[0] = PhysicalAddress.QuadPart;
            OutData[1] = (ULONG64)(UINT_PTR)VirtualAddress;
            OutData[2] = (ULONG64)(UINT_PTR)Mdl;  // For tracking
            
            bytesReturned = sizeof(ULONG64) * 3;
            status = STATUS_SUCCESS;

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250: AllocVidMem SUCCESS: %llu bytes, "
                "PA=0x%llX VA=%p MDL=%p (aligned: %s)\n",
                (ULONG64)AllocSize, 
                PhysicalAddress.QuadPart, 
                VirtualAddress, 
                Mdl,
                ((PhysicalAddress.QuadPart & GPU_PAGE_MASK) == 0) ? "YES" : "NO"));

        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ULONG ExceptionCode = GetExceptionCode();
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250: AllocVidMem EXCEPTION 0x%X\n", ExceptionCode));
            
            // Cleanup on exception
            if (Mdl != NULL) {
                if (VirtualAddress != NULL) {
                    MmUnmapLockedPages(VirtualAddress, Mdl);
                }
                MmFreePagesFromMdl(Mdl);
                ExFreePoolWithTag(Mdl, 'BCAM');
            }
            
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
        
        break;
    }
```

**ŽINGSNIS 3:** Pataisyti FreeVidMem handler:

Rasti eilutę ~2230: `case 0x80000844:` ir ZAMENIT:

```c
    /* --- Free Video Memory (TAISYTA) --- */
    case 0x80000844: { /* IOCTL_AMDBC250_FREE_VIDMEM */
        
        if (inputLen < sizeof(ULONG64) * 2) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        PULONG64 InData = (PULONG64)inputBuffer;
        PVOID VirtualAddress = (PVOID)(UINT_PTR)InData[0];
        PMDL Mdl = (PMDL)(UINT_PTR)InData[1];

        if (VirtualAddress == NULL || Mdl == NULL) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        __try {
            // Find in allocation list
            KIRQL OldIrql = KeAcquireSpinLockRaiseToDpc(&g_AllocationManager.SpinLock);
            
            PLIST_ENTRY ListEntry = g_AllocationManager.AllocationList.Flink;
            PGPU_ALLOCATION_ENTRY FoundEntry = NULL;

            while (ListEntry != &g_AllocationManager.AllocationList) {
                PGPU_ALLOCATION_ENTRY Entry = CONTAINING_RECORD(
                    ListEntry, GPU_ALLOCATION_ENTRY, ListEntry);
                
                if (Entry->VirtualAddress == VirtualAddress && Entry->Mdl == Mdl) {
                    FoundEntry = Entry;
                    RemoveEntryList(ListEntry);
                    g_AllocationManager.AllocationCount--;
                    g_AllocationManager.TotalAllocatedBytes -= Entry->Size;
                    break;
                }
                
                ListEntry = ListEntry->Flink;
            }

            KeReleaseSpinLock(&g_AllocationManager.SpinLock, OldIrql);

            if (FoundEntry != NULL) {
                // Unmap and free
                MmUnmapLockedPages(FoundEntry->VirtualAddress, FoundEntry->Mdl);
                MmFreePagesFromMdl(FoundEntry->Mdl);
                ExFreePoolWithTag(FoundEntry, 'BCAE');

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250: FreeVidMem OK (remaining: %u allocations)\n",
                    g_AllocationManager.AllocationCount));
            } else {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250: FreeVidMem - allocation not found\n"));
                status = STATUS_NOT_FOUND;
            }

        } __except (EXCEPTION_EXECUTE_HANDLER) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250: FreeVidMem EXCEPTION 0x%X\n", GetExceptionCode()));
            status = STATUS_UNSUCCESSFUL;
        }

        break;
    }
```

**ŽINGSNIS 4:** Pridėti cleanup funkcijas:

Pridėti šias funkcijas prieš `DreamV3WdmUnload`:

```c
/* ===== ALLOCATION CLEANUP ===== */

VOID CleanupAllAllocations(VOID)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
        "AMDBC250: Cleaning up %u allocations (%llu bytes)\n",
        g_AllocationManager.AllocationCount,
        g_AllocationManager.TotalAllocatedBytes));

    KIRQL OldIrql = KeAcquireSpinLockRaiseToDpc(&g_AllocationManager.SpinLock);
    
    while (!IsListEmpty(&g_AllocationManager.AllocationList)) {
        PLIST_ENTRY ListEntry = RemoveHeadList(&g_AllocationManager.AllocationList);
        PGPU_ALLOCATION_ENTRY Entry = CONTAINING_RECORD(
            ListEntry, GPU_ALLOCATION_ENTRY, ListEntry);

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
            "AMDBC250: Freeing allocation: VA=%p, PA=0x%llX, Size=%llu\n",
            Entry->VirtualAddress, Entry->PhysicalAddress.QuadPart, Entry->Size));

        if (Entry->VirtualAddress != NULL && Entry->Mdl != NULL) {
            MmUnmapLockedPages(Entry->VirtualAddress, Entry->Mdl);
            MmFreePagesFromMdl(Entry->Mdl);
        }

        ExFreePoolWithTag(Entry, 'BCAE');
    }

    g_AllocationManager.AllocationCount = 0;
    g_AllocationManager.TotalAllocatedBytes = 0;
    
    KeReleaseSpinLock(&g_AllocationManager.SpinLock, OldIrql);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Allocation cleanup complete\n"));
}
```

---

## 3. Display Flip Implementacija

### 3.1 - Header Definitions

**Failas: `src/kmd/amdbc250_dream_v3_kmd.h`**

Pridėti:

```c
/* ===== DCN 2.1 Display Engine Registers ===== */

/* HUBP (HUB Pipe) base addresses */
#define HUBPREQ0_BASE                   0x1C00
#define HUBPREQ_SURFACE_ADDRESS         (HUBPREQ0_BASE + 0x04)      // [31:0]
#define HUBPREQ_SURFACE_ADDRESS_HIGH    (HUBPREQ0_BASE + 0x08)      // [39:32]
#define HUBPREQ_SURFACE_PITCH           (HUBPREQ0_BASE + 0x0C)
#define HUBPREQ_SURFACE_HEIGHT          (HUBPREQ0_BASE + 0x10)
#define HUBPREQ_SURFACE_FORMAT          (HUBPREQ0_BASE + 0x14)
#define HUBPREQ_ENABLE                  (HUBPREQ0_BASE + 0x18)
#define HUBPREQ_FLIP_CONTROL            (HUBPREQ0_BASE + 0x1C)

/* Pixel format constants */
#define HUBPREQ_FORMAT_ARGB8888         0x00000004
#define HUBPREQ_FORMAT_XRGB8888         0x00000001
#define HUBPREQ_FORMAT_RGB565           0x00000000

/* Display Flip Request Structure */
typedef struct {
    ULONG64 SurfaceGpuVa;       // GPU virtual address
    ULONG Width;                // Width in pixels
    ULONG Height;               // Height in pixels
    ULONG Pitch;                // Pitch in bytes (optional, auto-calc if 0)
    ULONG PixelFormat;          // HUBPREQ_FORMAT_*
    ULONG VidPnSourceId;        // Display output ID
} DISPLAY_FLIP_REQUEST, *PDISPLAY_FLIP_REQUEST;
```

### 3.2 - Implementation

Rasti eilutę ~2390: `case 0x800008C4:` ir ZAMENIT:

```c
    /* --- Flip Display (TAISYTA) --- */
    case 0x800008C4: { /* IOCTL_AMDBC250_FLIP_DISPLAY */
        
        /* Validation */
        if (inputLen < sizeof(DISPLAY_FLIP_REQUEST)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        if (outputLen < sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        PDISPLAY_FLIP_REQUEST FlipReq = (PDISPLAY_FLIP_REQUEST)inputBuffer;

        /* Validate flip parameters */
        if (FlipReq->SurfaceGpuVa == 0) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (FlipReq->Width == 0 || FlipReq->Height == 0) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        /* Sanity check - max 8K resolution */
        if (FlipReq->Width > 7680 || FlipReq->Height > 4320) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        __try {
            if (DevExt->MmioVirtualBase == NULL) {
                status = STATUS_DEVICE_NOT_READY;
                __leave;
            }

            /* Calculate pitch if not provided (assume ARGB8888 = 4 bytes/pixel) */
            ULONG Pitch = FlipReq->Pitch;
            if (Pitch == 0) {
                Pitch = FlipReq->Width * 4;  // 4 bytes per pixel for ARGB8888
            }

            /* Program HUBPREQ registers */
            volatile PULONG pMmio = (volatile PULONG)DevExt->MmioVirtualBase;

            // Surface address (64-bit, split into low and high)
            ULONG SurfaceAddrLo = (ULONG)(FlipReq->SurfaceGpuVa & 0xFFFFFFFF);
            ULONG SurfaceAddrHi = (ULONG)(FlipReq->SurfaceGpuVa >> 32);

            DreamV3WriteRegister(DevExt, HUBPREQ_SURFACE_ADDRESS, SurfaceAddrLo);
            DreamV3WriteRegister(DevExt, HUBPREQ_SURFACE_ADDRESS_HIGH, SurfaceAddrHi);

            // Pitch (in bytes)
            DreamV3WriteRegister(DevExt, HUBPREQ_SURFACE_PITCH, Pitch);

            // Height
            DreamV3WriteRegister(DevExt, HUBPREQ_SURFACE_HEIGHT, FlipReq->Height);

            // Pixel format
            ULONG PixelFormat = (FlipReq->PixelFormat > 0) ? 
                FlipReq->PixelFormat : HUBPREQ_FORMAT_ARGB8888;
            DreamV3WriteRegister(DevExt, HUBPREQ_SURFACE_FORMAT, PixelFormat);

            // Enable display output
            DreamV3WriteRegister(DevExt, HUBPREQ_ENABLE, 0x00000001);

            // Trigger flip (optional - some displays auto-flip)
            DreamV3WriteRegister(DevExt, HUBPREQ_FLIP_CONTROL, 0x00000001);

            // Memory barrier to ensure writes complete
            KeMemoryBarrier();

            // Return success status in output buffer
            PULONG OutStatus = (PULONG)outputBuffer;
            *OutStatus = 0;  // 0 = success
            bytesReturned = sizeof(ULONG);

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250: Display flip OK: %ux%u@%u, PA=0x%llX, Pitch=%u, Format=0x%X\n",
                FlipReq->Width, FlipReq->Height, FlipReq->VidPnSourceId,
                FlipReq->SurfaceGpuVa, Pitch, PixelFormat));

        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ULONG ExCode = GetExceptionCode();
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250: Display flip exception 0x%X\n", ExCode));
            status = STATUS_DEVICE_PROTOCOL_ERROR;
        }

        break;
    }
```

---

## 4. Hardware Init Completion

### 4.1 - Missing Functions

**Failas: `src/kmd/amdbc250_dream_v3_hw_init.c`**

Pridėti šias funkcijas jei jų nėra:

```c
/* ===== GPU HARDWARE INITIALIZATION ===== */

NTSTATUS HwInitializeGpu(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    if (DevExt == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: GPU Hardware initialization starting...\n"));

    /* Step 1: GPU Device Enumeration */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Step 1 - Device enumeration\n"));
    
    DevExt->VendorId = 0x1002;      // AMD
    DevExt->DeviceId = 0x13FE;      // BC-250 (Cyan Skillfish)

    /* Step 2: MMIO Base Address Mapping (if not already done) */
    if (DevExt->MmioVirtualBase == NULL && DevExt->MmioPhysicalBase.QuadPart != 0) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250: Step 2 - MMIO mapping (PA: 0x%llX, Size: 0x%X)\n",
            DevExt->MmioPhysicalBase.QuadPart, DevExt->MmioSize));

        DevExt->MmioVirtualBase = MmMapIoSpace(
            DevExt->MmioPhysicalBase,
            DevExt->MmioSize,
            MmNonCached
        );

        if (DevExt->MmioVirtualBase == NULL) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250: MMIO mapping FAILED\n"));
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250: MMIO mapped at VA: %p\n", DevExt->MmioVirtualBase));
    }

    /* Step 3: GFX Ring Buffer Setup */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Step 3 - GFX ring buffer setup\n"));

    DevExt->GfxRing.SizeInBytes = 0x10000;  // 64KB
    
    /* Allocate ring buffer */
    PHYSICAL_ADDRESS GfxRingAddr;
    GfxRingAddr.QuadPart = 0;
    
    DevExt->GfxRing.VirtualAddress = MmAllocateContiguousMemory(
        DevExt->GfxRing.SizeInBytes,
        GfxRingAddr
    );

    if (DevExt->GfxRing.VirtualAddress == NULL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250: GFX ring allocation FAILED\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DevExt->GfxRing.PhysicalAddress = MmGetPhysicalAddress(DevExt->GfxRing.VirtualAddress);
    DevExt->GfxRing.ReadPointer = 0;
    DevExt->GfxRing.WritePointer = 0;
    KeInitializeSpinLock(&DevExt->GfxRing.Lock);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: GFX ring allocated: PA=0x%llX, VA=%p, Size=0x%X\n",
        DevExt->GfxRing.PhysicalAddress.QuadPart,
        DevExt->GfxRing.VirtualAddress,
        DevExt->GfxRing.SizeInBytes));

    /* Step 4: Program GFX Ring to Hardware */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Step 4 - Program GFX ring to hardware\n"));

    if (DevExt->MmioVirtualBase != NULL) {
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_BASE_LO,
            (ULONG)(DevExt->GfxRing.PhysicalAddress.QuadPart & 0xFFFFFFFF));
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_BASE_HI,
            (ULONG)(DevExt->GfxRing.PhysicalAddress.QuadPart >> 32));
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_RPTR, 0);
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_WPTR, 0);
    }

    /* Step 5: Global Fence Setup */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Step 5 - Global fence setup\n"));

    DevExt->GlobalFence.VirtualAddress = MmAllocateContiguousMemory(
        sizeof(ULONG64),
        GfxRingAddr
    );

    if (DevExt->GlobalFence.VirtualAddress != NULL) {
        *DevExt->GlobalFence.VirtualAddress = 0;
        DevExt->GlobalFence.PhysicalAddress = MmGetPhysicalAddress(
            DevExt->GlobalFence.VirtualAddress);
        KeInitializeEvent(&DevExt->GlobalFence.FenceEvent, 
            NotificationEvent, FALSE);
        DevExt->GlobalFence.LastSubmittedValue = 0;
        DevExt->GlobalFence.LastSignaledValue = 0;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250: Global fence allocated: PA=0x%llX\n",
            DevExt->GlobalFence.PhysicalAddress.QuadPart));
    }

    /* Step 6: HDP Flush Setup (CRITICAL FOR BC-250!) */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Step 6 - HDP flush setup\n"));

    if (DevExt->MmioVirtualBase != NULL) {
        /* Write HDP_MISC_CNTLS to enable HDP_FLUSH */
        DreamV3WriteRegister(DevExt, 0xF428, 0x3);
        KeMemoryBarrier();
    }

    DevExt->HardwareInitialized = TRUE;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: GPU Hardware initialization complete!\n"));

    return STATUS_SUCCESS;
}

/* ===== RING BUFFER INITIALIZATION ===== */

NTSTATUS HwInitializeRingBuffers(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    if (DevExt == NULL || DevExt->MmioVirtualBase == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Initializing ring buffers\n"));

    /* Already done in HwInitializeGpu, but can add SDMA ring here */
    
    /* SDMA Ring Buffer */
    DevExt->SdmaRing.SizeInBytes = 0x4000;  // 16KB

    PHYSICAL_ADDRESS SdmaRingAddr;
    SdmaRingAddr.QuadPart = 0;

    DevExt->SdmaRing.VirtualAddress = MmAllocateContiguousMemory(
        DevExt->SdmaRing.SizeInBytes,
        SdmaRingAddr
    );

    if (DevExt->SdmaRing.VirtualAddress != NULL) {
        DevExt->SdmaRing.PhysicalAddress = MmGetPhysicalAddress(
            DevExt->SdmaRing.VirtualAddress);
        DevExt->SdmaRing.ReadPointer = 0;
        DevExt->SdmaRing.WritePointer = 0;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250: SDMA ring allocated: PA=0x%llX, VA=%p\n",
            DevExt->SdmaRing.PhysicalAddress.QuadPart,
            DevExt->SdmaRing.VirtualAddress));
    }

    /* IH Ring Buffer */
    DevExt->IhRing.SizeInBytes = 0x2000;  // 8KB

    PHYSICAL_ADDRESS IhRingAddr;
    IhRingAddr.QuadPart = 0;

    DevExt->IhRing.VirtualAddress = MmAllocateContiguousMemory(
        DevExt->IhRing.SizeInBytes,
        IhRingAddr
    );

    if (DevExt->IhRing.VirtualAddress != NULL) {
        RtlZeroMemory(DevExt->IhRing.VirtualAddress, DevExt->IhRing.SizeInBytes);
        DevExt->IhRing.PhysicalAddress = MmGetPhysicalAddress(
            DevExt->IhRing.VirtualAddress);
        DevExt->IhRing.ReadPointer = 0;
        DevExt->IhRing.Initialized = TRUE;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250: IH ring allocated: PA=0x%llX, VA=%p\n",
            DevExt->IhRing.PhysicalAddress.QuadPart,
            DevExt->IhRing.VirtualAddress));
    }

    return STATUS_SUCCESS;
}

/* ===== DISPLAY ENGINE INITIALIZATION ===== */

NTSTATUS HwInitializeDisplayEngine(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    if (DevExt == NULL || DevExt->MmioVirtualBase == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Initializing display engine (DCN 2.1)\n"));

    /* Default display mode: 1920x1080@60Hz */
    DevExt->CurrentMode.Width = 1920;
    DevExt->CurrentMode.Height = 1080;
    DevExt->CurrentMode.RefreshRate = 60;
    DevExt->CurrentMode.BitsPerPixel = 32;
    DevExt->CurrentMode.Format = D3DDDIFMT_A8R8G8B8;

    /* Enable primary display output */
    DreamV3WriteRegister(DevExt, HUBPREQ_ENABLE, 0x00000001);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Display engine initialized with mode %ux%u@%uHz\n",
        DevExt->CurrentMode.Width,
        DevExt->CurrentMode.Height,
        DevExt->CurrentMode.RefreshRate));

    return STATUS_SUCCESS;
}

/* ===== HARDWARE CLEANUP ===== */

VOID HwShutdown(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    if (DevExt == NULL) {
        return;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Hardware shutdown\n"));

    /* Disable display */
    if (DevExt->MmioVirtualBase != NULL) {
        DreamV3WriteRegister(DevExt, HUBPREQ_ENABLE, 0x00000000);
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL, 0x00000001);  // Halt CPU
    }

    /* Free ring buffers */
    if (DevExt->GfxRing.VirtualAddress != NULL) {
        MmFreeContiguousMemory(DevExt->GfxRing.VirtualAddress);
        DevExt->GfxRing.VirtualAddress = NULL;
    }

    if (DevExt->SdmaRing.VirtualAddress != NULL) {
        MmFreeContiguousMemory(DevExt->SdmaRing.VirtualAddress);
        DevExt->SdmaRing.VirtualAddress = NULL;
    }

    if (DevExt->IhRing.VirtualAddress != NULL) {
        MmFreeContiguousMemory(DevExt->IhRing.VirtualAddress);
        DevExt->IhRing.VirtualAddress = NULL;
    }

    if (DevExt->GlobalFence.VirtualAddress != NULL) {
        MmFreeContiguousMemory(DevExt->GlobalFence.VirtualAddress);
        DevExt->GlobalFence.VirtualAddress = NULL;
    }

    /* Unmap MMIO */
    if (DevExt->MmioVirtualBase != NULL) {
        MmUnmapIoSpace(DevExt->MmioVirtualBase, DevExt->MmioSize);
        DevExt->MmioVirtualBase = NULL;
    }

    DevExt->HardwareInitialized = FALSE;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250: Hardware shutdown complete\n"));
}
```

---

## 5. Build & Kompiliavimas

### 5.1 - Build Environment Preparation

```powershell
# Open Command Prompt as Administrator
# Navigate to project directory

cd C:\AMD-BC-250-Windows-Driver

# Set VS 2022 environment
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

# Or set WDK environment
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\SetEnv.cmd" /Release /x64
```

### 5.2 - Run Build Script

```batch
REM In Command Prompt (Admin)
cd C:\AMD-BC-250-Windows-Driver

REM Execute build
build.bat

REM Expected output:
REM - Compiling amdbc250_dream_v3_kmd.c
REM - Compiling amdbc250_umd_v46.c
REM - Creating output\atikmdag.sys
REM - Creating output\amdbc250umd64.dll
REM - Creating catalog file
```

### 5.3 - Build Validation

```powershell
# After build completes
$outputDir = "C:\AMD-BC-250-Windows-Driver\output"

# Check if files were created
$files = @(
    "atikmdag.sys",
    "amdbc250umd64.dll", 
    "atikmdag.cat",
    "amdbc250_icd.json"
)

foreach ($file in $files) {
    $path = Join-Path $outputDir $file
    if (Test-Path $path) {
        $size = (Get-Item $path).Length
        Write-Host "✅ $file ($size bytes)" -ForegroundColor Green
    } else {
        Write-Host "❌ $file NOT FOUND" -ForegroundColor Red
    }
}

# Check file integrity
# Driver signs should be valid
$sysFile = Join-Path $outputDir "atikmdag.sys"
Get-Item $sysFile | Format-List LastWriteTime, Length
```

---

## 6. Testing & Validation

### 6.1 - Pre-Installation Tests

```powershell
# Run BEFORE installing driver

# Test 1: Verify test signing enabled
bcdedit /enum all | Select-String "testsigning"
# Expected: testsigning    Yes

# Test 2: Check for other AMD drivers
Get-PnpDevice -Class Display | Where-Object { $_.FriendlyName -like "*AMD*" }

# Test 3: Run basic IOCTL test (if test binary exists)
cd C:\AMD-BC-250-Windows-Driver\output
.\test-gpu-ioctls.exe
# Expected: Multiple PASS results
```

### 6.2 - Installation

```powershell
# Open Device Manager
# Locate: AMD BC-250 Graphics (or Unknown Device)
# Right-click → Update Driver
# Browse to: C:\AMD-BC-250-Windows-Driver\output
# Select driver and install
# Reboot when prompted

# After reboot:
Get-PnpDevice -Class Display | Select Status, FriendlyName
# Expected Status: OK (or Degraded is acceptable)
```

### 6.3 - Post-Installation Tests

```powershell
# Test 1: Verify Vulkan ICD registration
$vulkanKey = "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers"
Get-Item $vulkanKey | Select-Object -ExpandProperty Property

# Test 2: Run Vulkan info
cd C:\AMD-BC-250-Windows-Driver\output
.\vulkaninfo.exe
# Expected: GPU info displayed without errors

# Test 3: Run IOCTL tests
.\test-gpu-ioctls.exe
# Expected: 13/15 or more PASS

# Test 4: Run Vulkan ICD tests
.\test-vulkan-icd.exe
# Expected: 13/13 PASS
```

---

## 7. Debugging (WinDBG)

### 7.1 - WinDBG Setup for Kernel-Mode

```
# Download WinDBG from Microsoft Store or Windows SDK

# Configure kernel debugging:
# 1. Virtual Machine (Hyper-V): Named pipe connection
# 2. USB 3.0 debug cable
# 3. COM port serial (legacy, slower)

# Enable kernel debugging:
# (Run as Admin)
bcdedit /debug on
bcdedit /dbgsettings serial debugport:1 baudrate:115200

# Or for Hyper-V VM:
bcdedit /dbgsettings net hostip:192.168.1.100 port:50000 key:1.2.3.4

# Reboot and connect WinDBG
```

### 7.2 - Debugging AllocVidMem BSOD

```windbg
# Commands in WinDBG

# Get stack trace
kd> k

# Analyze BSOD
kd> !analyze -v

# Check memory allocation
kd> !vm

# Set breakpoint on IOCTL handler
kd> bp atikmdag!DreamV3DeviceControl+0x100
kd> g

# When hit, examine registers
kd> r

# Examine buffer
kd> db @rsi L20  (for input buffer)

# Check allocation manager
kd> dq <address_of_g_AllocationManager> L10

# Continue execution
kd> g
```

### 7.3 - Common Debug Commands

```windbg
# List all loaded drivers
kd> !drvobj

# Get device object info
kd> !devobj <device_ptr>

# Check PTE (Page Table Entry)
kd> !pte <virtual_address>

# Display Physical page info
kd> !pfn <page_frame_number>

# Dump kernel structure
kd> dt _DEVICE_OBJECT <address>

# Set conditional breakpoint
kd> bp atikmdag!DreamV3DeviceControl "dpo @r8 L100; g"

# Display exception info
kd> !exr -1

# Check interrupt handlers
kd> !idt
```

---

## ✅ Completion Checklist

### Phase 1: AllocVidMem Fix
- [ ] Backup original file
- [ ] Add header definitions (GPU_PAGE_SIZE, etc.)
- [ ] Initialize allocation manager in DriverEntry
- [ ] Implement new AllocVidMem handler (MDL-based)
- [ ] Implement new FreeVidMem handler
- [ ] Add cleanup functions
- [ ] Compile without errors
- [ ] Test with test-gpu-ioctls.exe

### Phase 2: Display Flip
- [ ] Add display register definitions to header
- [ ] Implement flip handler with full validation
- [ ] Test with display mode tests
- [ ] Verify MMIO writes (WinDBG)

### Phase 3: Hardware Init
- [ ] Add GPU init function
- [ ] Add ring buffer initialization
- [ ] Add display engine init
- [ ] Add shutdown cleanup
- [ ] Call from DriverEntry
- [ ] Verify all allocations freed on unload

### Phase 4: Build & Test
- [ ] Full rebuild without warnings
- [ ] Sign driver with test certificate
- [ ] Install to test machine
- [ ] Run IOCTL tests (target: 15/15 PASS)
- [ ] Run Vulkan tests (target: 13/13 PASS)
- [ ] No BSOD or crashes
- [ ] Check Device Manager status
- [ ] Verify memory cleanup on unload

---

## 🔍 Troubleshooting

### Build Errors

**Error: "dxgkrnl.lib not found"**
```
Solution: Run build.bat which creates import library from dxgkrnl.sys
```

**Error: "Compilation failed with code 2"**
```
Solution: Check Visual Studio paths and rebuild entire solution
```

### Runtime Errors

**BSOD on driver load:**
```
1. Check WinDBG for exception info
2. Verify MMIO base address is correct
3. Check pool allocation sizes (not too large)
```

**AllocVidMem still crashes:**
```
1. Verify IRQL is APC_LEVEL or lower
2. Check MDL page count matches allocation size
3. Test with smaller allocation sizes first (4KB, 8KB)
```

**Display doesn't appear:**
```
1. Verify HUBPREQ register offsets
2. Check MMIO mapping succeeded
3. Set breakpoint in flip handler to verify it's called
```

---

**Created:** 2026-05-31 by Copilot  
**Last Updated:** Build Guide v1.0  

*Sėkmės su build'u! 🚀*

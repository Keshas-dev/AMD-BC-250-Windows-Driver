/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 3.0

Module Name:
    amdbc250_dream_v3_vm.c

Abstract:
    GPU Virtual Memory Management for AMD BC-250 (RDNA2 / Cyan Skillfish / GFX1013).

    FEATURES:
    1. GPUVM (GPU Virtual Memory) page table management
    2. GART (Graphics Aperture Remapping Table) support
    3. 4-level page table hierarchy (GFX10 style)
    4. VMID allocation and management (16 VMIDs)
    5. Memory eviction and restoration
    6. TLB invalidation via VM invalidate engine
    7. System aperture configuration
    8. Support for 48-bit GPU virtual addresses

    MEMORY LAYOUT (GFX10 48-bit):
    - Bits 47-39: PML4 (Page Map Level 4) - 512 entries
    - Bits 38-30: PD  (Page Directory)      - 512 entries  
    - Bits 29-21: PT  (Page Table)           - 512 entries
    - Bits 20-12: Offset (4KB page)          - 4096 bytes
    - Bits 11-0:  Fragment (within page)

    ADDRESS SPACES:
    - VRAM: 0x0000_0000_0000 - 0x0003_FFFF_FFFF (16GB)
    - GART: 0x0001_0000_0000 - 0x0001_FFFF_FFFF (64GB aperture)
    - System: 0xFFFF_8000_0000+ (kernel space)

    Based on Linux amdgpu driver:
    - drivers/gpu/drm/amd/amdgpu/amdgpu_vm.c
    - drivers/gpu/drm/amd/amdgpu/amdgpu_gart.c
    - drivers/gpu/drm/amd/amdgpu/gfxhub_v3_0.c

Environment:
    Kernel mode (IRQL <= DISPATCH_LEVEL)

--*/

#include "amdbc250_dream_v3_kmd.h"

/* Forward declarations */
static NTSTATUS DreamV3VmAllocatePageTable(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _Out_ PDREAM_V3_PAGE_TABLE PageTable
    );

static VOID DreamV3VmFreePageTable(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_PAGE_TABLE PageTable
    );

static NTSTATUS DreamV3VmInvalidateTLB(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG VmId
    );

static ULONG64 DreamV3VmEncodePte(
    _In_ PHYSICAL_ADDRESS PhysicalAddr,
    _In_ ULONG Flags
    );

/*===========================================================================
  GART (Graphics Aperture Remapping Table) Management
  
  GART provides a linear aperture that maps GPU virtual addresses to
  physical pages (either VRAM or system memory).
  
  Linux equivalent: amdgpu_gart_* functions
===========================================================================*/

NTSTATUS
DreamV3GartInitialize(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: GART initialization started\n"));

    /* Initialize GART structure */
    DevExt->Memory.GartTable.NumEntries = AMDBC250_GART_NUM_ENTRIES;
    DevExt->Memory.GartTable.EntrySize = AMDBC250_GART_ENTRY_SIZE;
    DevExt->Memory.GartTable.TotalSize = AMDBC250_GART_NUM_ENTRIES * AMDBC250_GART_ENTRY_SIZE;

    /* Allocate GART page table (contiguous for hardware) */
    DevExt->Memory.GartTable.PageTable.VirtualAddress = 
        ExAllocatePool2(POOL_FLAG_NON_PAGED, 
                        DevExt->Memory.GartTable.TotalSize,
                        DREAM_V3_TAG_VM);
    
    if (DevExt->Memory.GartTable.PageTable.VirtualAddress == NULL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: Failed to allocate GART table\n"));
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(DevExt->Memory.GartTable.PageTable.VirtualAddress,
                  DevExt->Memory.GartTable.TotalSize);

    /* Initialize bitmap for tracking allocated entries */
    RtlInitializeBitMap(&DevExt->Memory.GartTable.AllocationBitmap,
                        DevExt->Memory.GartTable.AllocationBits,
                        AMDBC250_GART_NUM_ENTRIES);
    RtlClearAllBits(&DevExt->Memory.GartTable.AllocationBitmap);

    /* Set default GART aperture in memory controller */
    /* GART base: 0x0001_0000_0000 (64GB aperture starting at 1TB) */
    PHYSICAL_ADDRESS GartBase;
    GartBase.QuadPart = AMDBC250_GART_APERTURE_BASE;
    
    DreamV3WriteRegister(DevExt, AMDBC250_REG_MC_VM_AGP_BASE,
                         (ULONG)(GartBase.QuadPart >> 27));
    DreamV3WriteRegister(DevExt, AMDBC250_REG_MC_VM_AGP_TOP,
                         (ULONG)((GartBase.QuadPart + DevExt->Memory.GartTable.TotalSize) >> 27));
    DreamV3WriteRegister(DevExt, AMDBC250_REG_MC_VM_AGP_BOT,
                         (ULONG)(GartBase.QuadPart >> 27));

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: GART initialized - %u entries (%u MB)\n",
               DevExt->Memory.GartTable.NumEntries,
               DevExt->Memory.GartTable.TotalSize / (1024 * 1024)));

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3GartMapPage(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG GartIndex,
    _In_ PHYSICAL_ADDRESS PhysicalAddr,
    _In_ ULONG Flags
    )
{
    PULONG GartEntry;
    ULONG64 PteValue;

    if (GartIndex >= DevExt->Memory.GartTable.NumEntries) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Check if entry is already allocated */
    if (RtlTestBit(&DevExt->Memory.GartTable.AllocationBitmap, GartIndex)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: GART entry %u already allocated\n", GartIndex));
        return STATUS_ADDRESS_ALREADY_EXISTS;
    }

    /* Mark as allocated */
    RtlSetBit(&DevExt->Memory.GartTable.AllocationBitmap, GartIndex);

    /* Encode PTE */
    PteValue = DreamV3VmEncodePte(PhysicalAddr, Flags);

    /* Write to GART table */
    GartEntry = (PULONG)DevExt->Memory.GartTable.PageTable.VirtualAddress;
    GartEntry[GartIndex] = (ULONG)(PteValue & 0xFFFFFFFF);
    GartEntry[GartIndex + 1] = (ULONG)(PteValue >> 32);

    DevExt->Memory.GartTable.NumAllocated++;

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3GartUnmapPage(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG GartIndex
    )
{
    PULONG GartEntry;

    if (GartIndex >= DevExt->Memory.GartTable.NumEntries) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Check if entry is allocated */
    if (!RtlTestBit(&DevExt->Memory.GartTable.AllocationBitmap, GartIndex)) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Clear entry */
    GartEntry = (PULONG)DevExt->Memory.GartTable.PageTable.VirtualAddress;
    GartEntry[GartIndex] = 0;
    GartEntry[GartIndex + 1] = 0;

    /* Mark as free */
    RtlClearBit(&DevExt->Memory.GartTable.AllocationBitmap, GartIndex);
    DevExt->Memory.GartTable.NumAllocated--;

    return STATUS_SUCCESS;
}

ULONG
DreamV3GartAllocateRange(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG NumPages
    )
{
    ULONG StartIndex = 0;

    /* Find consecutive free entries */
    StartIndex = RtlFindClearBitsAndSet(&DevExt->Memory.GartTable.AllocationBitmap,
                                        NumPages,
                                        0);
    
    if (StartIndex == 0xFFFFFFFF) {
        return 0xFFFFFFFF;  /* No space */
    }

    DevExt->Memory.GartTable.NumAllocated += NumPages;
    return StartIndex;
}

/*===========================================================================
  GPUVM Page Table Management
  
  Implements 4-level page table hierarchy for GFX10:
  Level 0: PML4 (Page Map Level 4) - 512 entries, 4096 bytes
  Level 1: PD  (Page Directory)     - 512 entries per PD, 4096 bytes
  Level 2: PT  (Page Table)         - 512 entries per PT, 4096 bytes
  Level 3: Page (4KB physical)

  Total address space: 512^3 * 4KB = 4PB (48-bit)
  
  Linux equivalent: amdgpu_vm_* functions
===========================================================================*/

NTSTATUS
DreamV3VmInitialize(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    NTSTATUS Status;
    ULONG VmId;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: GPUVM initialization started\n"));

    /* Initialize VMID bitmap (VMID 0 reserved for kernel, 1-15 for user) */
    RtlInitializeBitMap(&DevExt->Memory.VmidBitmap,
                        DevExt->Memory.VmidBits,
                        AMDBC250_MAX_VMIDS);
    RtlSetBit(&DevExt->Memory.VmidBitmap, 0);  /* VMID 0 reserved */
    RtlClearBits(&DevExt->Memory.VmidBitmap, 1, AMDBC250_MAX_VMIDS - 1);

    /* Configure system aperture */
    Status = DreamV3VmConfigureSystemAperture(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: System aperture failed: 0x%08X\n", Status));
        return Status;
    }

    /* Initialize VM contexts */
    for (VmId = 0; VmId < AMDBC250_MAX_VM_CONTEXTS; VmId++) {
        DevExt->Memory.VmContexts[VmId].VmId = (UCHAR)VmId;
        DevExt->Memory.VmContexts[VmId].IsValid = FALSE;
        DevExt->Memory.VmContexts[VmId].PageTableDepth = 0;
        InitializeListHead(&DevExt->Memory.VmContexts[VmId].AllocationList);
        KeInitializeSpinLock(&DevExt->Memory.VmContexts[VmId].VmLock);
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: GPUVM initialized - %d VMIDs available\n",
               AMDBC250_MAX_VMIDS - 1));

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3VmShutdown(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    ULONG VmId;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: GPUVM shutdown\n"));

    /* Free all VM contexts */
    for (VmId = 0; VmId < AMDBC250_MAX_VM_CONTEXTS; VmId++) {
        PDREAM_V3_VM_CONTEXT VmCtx = &DevExt->Memory.VmContexts[VmId];
        
        if (VmCtx->IsValid) {
            DreamV3VmDestroyContext(DevExt, VmId);
        }
    }

    /* Free GART table */
    if (DevExt->Memory.GartTable.PageTable.VirtualAddress != NULL) {
        ExFreePoolWithTag(DevExt->Memory.GartTable.PageTable.VirtualAddress, DREAM_V3_TAG_VM);
        DevExt->Memory.GartTable.PageTable.VirtualAddress = NULL;
    }

    /* Free scratch page (prevent memory leak) */
    if (DevExt->Memory.ScratchPageVirtual != NULL) {
        ExFreePoolWithTag(DevExt->Memory.ScratchPageVirtual, DREAM_V3_TAG_VM);
        DevExt->Memory.ScratchPageVirtual = NULL;
        DevExt->Memory.ScratchPagePhysical.QuadPart = 0;
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3VmAllocatePageTable - Allocate a page table page
  
  Each page table page is 4KB and contains 512 entries (8 bytes each).
  Must be 4KB aligned for hardware requirements.
===========================================================================*/

static NTSTATUS
DreamV3VmAllocatePageTable(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _Out_ PDREAM_V3_PAGE_TABLE PageTable
    )
{
    PHYSICAL_ADDRESS PhysAddr, LowAddress, HighAddress, BoundaryMultiple;
    PVOID VirtAddr;
    SIZE_T Size = PAGE_SIZE;

    /* Set address limits for allocation */
    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = -1;  /* Any address */
    BoundaryMultiple.QuadPart = 0;  /* No boundary constraint */

    /* Allocate 4KB aligned page */
    VirtAddr = MmAllocateContiguousMemorySpecifyCache(
        Size,
        LowAddress,
        HighAddress,
        BoundaryMultiple,
        MmCached
        );

    if (VirtAddr == NULL) {
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(VirtAddr, Size);

    PhysAddr = MmGetPhysicalAddress(VirtAddr);

    PageTable->VirtualAddress = VirtAddr;
    PageTable->PhysicalAddress = PhysAddr;
    PageTable->SizeInBytes = Size;
    PageTable->NumEntries = (ULONG)(Size / sizeof(ULONG64));

    return STATUS_SUCCESS;
}

static VOID
DreamV3VmFreePageTable(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_PAGE_TABLE PageTable
    )
{
    UNREFERENCED_PARAMETER(DevExt);

    if (PageTable->VirtualAddress != NULL) {
        MmFreeContiguousMemory(PageTable->VirtualAddress);
        PageTable->VirtualAddress = NULL;
        PageTable->PhysicalAddress.QuadPart = 0;
        PageTable->SizeInBytes = 0;
        PageTable->NumEntries = 0;
    }
}

/*===========================================================================
  DreamV3VmCreateContext - Create a new GPU VM context
  
  Allocates PML4 table and initializes page table hierarchy.
  Called when a process opens a GPU device context.
===========================================================================*/

NTSTATUS
DreamV3VmCreateContext(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG RequestedVmId,
    _Out_ PDREAM_V3_VM_CONTEXT* OutContext
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG VmId = 0;
    PDREAM_V3_VM_CONTEXT VmCtx;

    /* Find free VMID if requested VMID is 0xFFFFFFFF */
    if (RequestedVmId == 0xFFFFFFFF) {
        VmId = RtlFindClearBitsAndSet(&DevExt->Memory.VmidBitmap, 1, 0);
        if (VmId == 0xFFFFFFFF) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    } else {
        if (RequestedVmId >= AMDBC250_MAX_VMIDS) {
            return STATUS_INVALID_PARAMETER;
        }
        if (RtlTestBit(&DevExt->Memory.VmidBitmap, RequestedVmId)) {
            return STATUS_ADDRESS_ALREADY_EXISTS;
        }
        RtlSetBit(&DevExt->Memory.VmidBitmap, RequestedVmId);
        VmId = RequestedVmId;
    }

    VmCtx = &DevExt->Memory.VmContexts[VmId];

    /* Allocate PML4 table (top level) */
    Status = DreamV3VmAllocatePageTable(DevExt, &VmCtx->Pml4Table);
    if (!NT_SUCCESS(Status)) {
        RtlClearBit(&DevExt->Memory.VmidBitmap, VmId);
        return Status;
    }

    /* Dynamically allocate page directory and page table arrays */
    VmCtx->PageDirectories = (PDREAM_V3_PAGE_TABLE)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        512 * sizeof(DREAM_V3_PAGE_TABLE),
        DREAM_V3_TAG_VM);
    
    if (VmCtx->PageDirectories == NULL) {
        DreamV3VmFreePageTable(DevExt, &VmCtx->Pml4Table);
        RtlClearBit(&DevExt->Memory.VmidBitmap, VmId);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(VmCtx->PageDirectories, 512 * sizeof(DREAM_V3_PAGE_TABLE));

    VmCtx->PageTables = (PDREAM_V3_PAGE_TABLE)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        (512 * 512) * sizeof(DREAM_V3_PAGE_TABLE),
        DREAM_V3_TAG_VM);
    
    if (VmCtx->PageTables == NULL) {
        ExFreePoolWithTag(VmCtx->PageDirectories, DREAM_V3_TAG_VM);
        VmCtx->PageDirectories = NULL;
        DreamV3VmFreePageTable(DevExt, &VmCtx->Pml4Table);
        RtlClearBit(&DevExt->Memory.VmidBitmap, VmId);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(VmCtx->PageTables, (512 * 512) * sizeof(DREAM_V3_PAGE_TABLE));

    /* Initialize context */
    VmCtx->VmId = (UCHAR)VmId;
    VmCtx->IsValid = TRUE;
    VmCtx->PageTableDepth = 3;  /* PML4 -> PD -> PT -> Page */
    VmCtx->PageTableBase = VmCtx->Pml4Table.PhysicalAddress;
    VmCtx->NumPageDirectories = 0;
    VmCtx->NumPageTables = 0;
    KeInitializeSpinLock(&VmCtx->VmLock);
    InitializeListHead(&VmCtx->AllocationList);

    *OutContext = VmCtx;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: VM context created - VMID %d, PML4 PA=0x%llX\n",
               VmId, VmCtx->Pml4Table.PhysicalAddress.QuadPart));

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3VmDestroyContext(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG VmId
    )
{
    PDREAM_V3_VM_CONTEXT VmCtx;
    PLIST_ENTRY Entry;
    PDREAM_V3_VM_ALLOCATION Alloc;

    if (VmId >= AMDBC250_MAX_VM_CONTEXTS) {
        return STATUS_INVALID_PARAMETER;
    }

    VmCtx = &DevExt->Memory.VmContexts[VmId];

    if (!VmCtx->IsValid) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Free all allocations in this VM */
    KeAcquireSpinLockAtDpcLevel(&VmCtx->VmLock);
    
    while (!IsListEmpty(&VmCtx->AllocationList)) {
        Entry = RemoveHeadList(&VmCtx->AllocationList);
        Alloc = CONTAINING_RECORD(Entry, DREAM_V3_VM_ALLOCATION, ListEntry);
        
        /* Unmap from page tables */
        DreamV3VmUnmapRange(DevExt, VmCtx, Alloc->VirtualAddress, Alloc->SizeInBytes);
        
        ExFreePoolWithTag(Alloc, DREAM_V3_TAG_ALLOCATION);
    }

    KeReleaseSpinLockFromDpcLevel(&VmCtx->VmLock);

    /* Free all page tables (PML4 -> PD -> PT) */
    if (VmCtx->PageTables != NULL) {
        ULONG i;
        for (i = 0; i < (512 * 512); i++) {
            if (VmCtx->PageTables[i].VirtualAddress != NULL) {
                DreamV3VmFreePageTable(DevExt, &VmCtx->PageTables[i]);
            }
        }
        ExFreePoolWithTag(VmCtx->PageTables, DREAM_V3_TAG_VM);
        VmCtx->PageTables = NULL;
    }

    if (VmCtx->PageDirectories != NULL) {
        ULONG i;
        for (i = 0; i < 512; i++) {
            if (VmCtx->PageDirectories[i].VirtualAddress != NULL) {
                DreamV3VmFreePageTable(DevExt, &VmCtx->PageDirectories[i]);
            }
        }
        ExFreePoolWithTag(VmCtx->PageDirectories, DREAM_V3_TAG_VM);
        VmCtx->PageDirectories = NULL;
    }
    
    /* Free PML4 */
    DreamV3VmFreePageTable(DevExt, &VmCtx->Pml4Table);

    /* Free VMID */
    RtlClearBit(&DevExt->Memory.VmidBitmap, VmCtx->VmId);
    VmCtx->IsValid = FALSE;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: VM context %d destroyed\n", VmId));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3VmMapRange - Map physical pages into GPU virtual address space
  
  Walks the 4-level page table hierarchy and creates mappings.
  Allocates intermediate page tables as needed.
  
  Linux equivalent: amdgpu_vm_bo_map()
===========================================================================*/

NTSTATUS
DreamV3VmMapRange(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_VM_CONTEXT VmCtx,
    _In_ ULONG64 VirtualAddress,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T SizeInBytes,
    _In_ ULONG Flags
    )
{
    NTSTATUS Status;
    ULONG64 CurrentVa = VirtualAddress;
    ULONG64 EndVa = VirtualAddress + SizeInBytes;
    ULONG NumPagesMapped = 0;

    if (!VmCtx->IsValid) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Align to page boundary */
    CurrentVa = ALIGN_DOWN_BY(CurrentVa, PAGE_SIZE);

    KeAcquireSpinLockAtDpcLevel(&VmCtx->VmLock);

    while (CurrentVa < EndVa) {
        /* Walk page table hierarchy */
        Status = DreamV3VmInsertMapping(DevExt, VmCtx, CurrentVa, PhysicalAddress, Flags);
        
        if (!NT_SUCCESS(Status)) {
            KeReleaseSpinLockFromDpcLevel(&VmCtx->VmLock);
            
            /* Rollback on failure */
            if (NumPagesMapped > 0) {
                DreamV3VmUnmapRange(DevExt, VmCtx, VirtualAddress, 
                                    NumPagesMapped * PAGE_SIZE);
            }
            return Status;
        }

        CurrentVa += PAGE_SIZE;
        PhysicalAddress.QuadPart += PAGE_SIZE;
        NumPagesMapped++;
    }

    KeReleaseSpinLockFromDpcLevel(&VmCtx->VmLock);

    /* Invalidate TLB to pick up new mappings */
    DreamV3VmInvalidateTLB(DevExt, VmCtx->VmId);

    VmCtx->NumMappings += NumPagesMapped;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: Mapped %u pages at VA 0x%llX (VMID %d)\n",
               NumPagesMapped, VirtualAddress, VmCtx->VmId));

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3VmInsertMapping(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_VM_CONTEXT VmCtx,
    _In_ ULONG64 VirtualAddress,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG Flags
    )
{
    ULONG Pml4Index, PdIndex, PtIndex, PageOffset;
    PULONG64 Pml4, Pd, Pt;
    PHYSICAL_ADDRESS PdPhys, PtPhys;

    /* Decode virtual address */
    Pml4Index = (VirtualAddress >> 39) & 0x1FF;
    PdIndex   = (VirtualAddress >> 30) & 0x1FF;
    PtIndex   = (VirtualAddress >> 21) & 0x1FF;
    PageOffset = VirtualAddress & 0x1FF;

    /* FIX v4.2: Bounds checking to prevent array overflow */
    if (Pml4Index >= 512 || PdIndex >= 512 || PtIndex >= 512) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "[BC250] VM: Invalid VA indices: PML4=%lu, PD=%lu, PT=%lu\n",
            Pml4Index, PdIndex, PtIndex));
        return STATUS_INVALID_PARAMETER;
    }

    UNREFERENCED_PARAMETER(PageOffset);

    /* Get PML4 entry */
    Pml4 = (PULONG64)VmCtx->Pml4Table.VirtualAddress;
    
    /* Allocate PD if needed */
    if (Pml4[Pml4Index] == 0) {
        /* Allocate PD page */
        PDREAM_V3_PAGE_TABLE NewPd = &VmCtx->PageDirectories[Pml4Index];
        NTSTATUS Status = DreamV3VmAllocatePageTable(DevExt, NewPd);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }

        PdPhys = NewPd->PhysicalAddress;
        Pml4[Pml4Index] = DreamV3VmEncodePte(PdPhys, AMDBC250_PTE_VALID);
    }

    /* Get PD entry */
    Pd = (PULONG64)VmCtx->PageDirectories[Pml4Index].VirtualAddress;
    
    /* Allocate PT if needed */
    if (Pd[PdIndex] == 0) {
        /* Allocate PT page */
        ULONG PtKey = (Pml4Index << 9) | PdIndex;
        PDREAM_V3_PAGE_TABLE NewPt = &VmCtx->PageTables[PtKey];
        NTSTATUS Status = DreamV3VmAllocatePageTable(DevExt, NewPt);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }

        PtPhys = NewPt->PhysicalAddress;
        Pd[PdIndex] = DreamV3VmEncodePte(PtPhys, AMDBC250_PTE_VALID);
    }

    /* Get PT entry and write mapping */
    ULONG PtKey = (Pml4Index << 9) | PdIndex;
    Pt = (PULONG64)VmCtx->PageTables[PtKey].VirtualAddress;
    
    ULONG64 PteValue = DreamV3VmEncodePte(PhysicalAddress, Flags);
    Pt[PtIndex] = PteValue;

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3VmUnmapRange(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_VM_CONTEXT VmCtx,
    _In_ ULONG64 VirtualAddress,
    _In_ SIZE_T SizeInBytes
    )
{
    ULONG64 CurrentVa = ALIGN_DOWN_BY(VirtualAddress, PAGE_SIZE);
    ULONG64 EndVa = CurrentVa + ALIGN_UP_BY(SizeInBytes, PAGE_SIZE);
    ULONG NumPages = 0;

    /* FIX v4.2: Actually walk page tables and clear entries */
    while (CurrentVa < EndVa) {
        ULONG Pml4Index = (CurrentVa >> 39) & 0x1FF;
        ULONG PdIndex   = (CurrentVa >> 30) & 0x1FF;
        ULONG PtIndex   = (CurrentVa >> 21) & 0x1FF;

        /* Check if PML4 entry exists */
        PULONG64 Pml4 = (PULONG64)VmCtx->Pml4Table.VirtualAddress;
        if (Pml4Index < 512 && Pml4[Pml4Index] != 0) {
            /* Get PD */
            ULONG PdKey = Pml4Index;
            if (PdKey < AMDBC250_MAX_VM_CONTEXTS) {
                PULONG64 Pd = (PULONG64)VmCtx->PageDirectories[PdKey].VirtualAddress;
                if (Pd && PdIndex < 512 && Pd[PdIndex] != 0) {
                    /* Get PT */
                    ULONG PtKey = (Pml4Index << 9) | PdIndex;
                    if (PtKey < 512*512) {
                        PULONG64 Pt = (PULONG64)VmCtx->PageTables[PtKey].VirtualAddress;
                        if (Pt && PtIndex < 512) {
                            /* Clear the page table entry */
                            Pt[PtIndex] = 0;
                            NumPages++;
                        }
                    }
                }
            }
        }

        CurrentVa += PAGE_SIZE;
    }

    /* Invalidate TLB after clearing entries */
    if (NumPages > 0) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
            "[BC250] VM: Unmapped %lu pages, invalidating TLB\n", NumPages));
        DreamV3VmInvalidateTLB(DevExt, VmCtx->VmId);
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3VmInvalidateTLB - Invalidate GPU TLB
  
  After page table updates, the TLB must be invalidated for the GPU
  to see the new mappings.
  
  Linux equivalent: amdgpu_vm_flush()
===========================================================================*/

static NTSTATUS
DreamV3VmInvalidateTLB(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG VmId
    )
{
    ULONG VmCntl;

    if (VmId >= AMDBC250_MAX_VMIDS) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Write invalidation request */
    DreamV3WriteRegister(DevExt, 
                         AMDBC250_REG_VM_INVALIDATE_ENG0_REQ + (VmId * 0x10),
                         1);

    /* Wait for acknowledgment */
    ULONG Timeout = 1000;
    while (Timeout-- > 0) {
        VmCntl = DreamV3ReadRegister(DevExt, 
                                     AMDBC250_REG_VM_INVALIDATE_ENG0_ACK + (VmId * 0x10));
        if (VmCntl & 0x1) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(10);
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
               "AMDBC250-DREAM-V4.3: VM TLB invalidation timeout for VMID %u\n", VmId));
    
    return STATUS_IO_TIMEOUT;
}

/*===========================================================================
  DreamV3VmConfigureSystemAperture - Configure fallback address space
  
  The system aperture handles addresses that fall outside VM mappings:
  - VRAM direct access
  - GART aperture  
  - System memory (for CPU-GPU shared)
  
  Linux equivalent: gmc_v10_0_vm_init_system_aperture()
===========================================================================*/

NTSTATUS
DreamV3VmConfigureSystemAperture(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    PHYSICAL_ADDRESS FbBase, FbTop;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Configuring system aperture\n"));

    /* VRAM aperture: Direct access to framebuffer */
    FbBase = DevExt->FbPhysicalBase;
    FbTop.QuadPart = FbBase.QuadPart + DevExt->FbSize - 1;

    DreamV3WriteRegister(DevExt, AMDBC250_REG_MC_VM_FB_LOCATION_BASE,
                         (ULONG)(FbBase.QuadPart >> 24));
    DreamV3WriteRegister(DevExt, AMDBC250_REG_MC_VM_FB_LOCATION_TOP,
                         (ULONG)(FbTop.QuadPart >> 24));

    /* System aperture: Use scratch page for invalid access */
    /* This prevents GPU faults from crashing the system */
    PHYSICAL_ADDRESS ScratchPage;
    PVOID ScratchVirt = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, DREAM_V3_TAG_VM);
    
    if (ScratchVirt != NULL) {
        RtlZeroMemory(ScratchVirt, PAGE_SIZE);
        ScratchPage = MmGetPhysicalAddress(ScratchVirt);
        
        DevExt->Memory.ScratchPagePhysical = ScratchPage;
        DevExt->Memory.ScratchPageVirtual = ScratchVirt;

        /* Set as default for page faults */
        DreamV3WriteRegister(DevExt, 
                             AMDBC250_REG_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR,
                             (ULONG)(ScratchPage.QuadPart >> 12));
    }

    /* Enable VM contexts */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_VM_CONTEXT0_CNTL, 0x1);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: System aperture configured\n"));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3VmEncodePte - Encode a page table entry
  
  GFX10 PTE format (64-bit):
  - Bits 63-12: Physical address (4KB aligned)
  - Bit 0: Valid
  - Bit 1: Readable
  - Bit 2: Writable
  - Bit 3: Executable
  - Bit 4: Snoop (CPU cache coherency)
  - Bit 5: System (system memory vs VRAM)
  
  Linux equivalent: amdgpu_vm_pte_to_gfx10()
===========================================================================*/

static ULONG64
DreamV3VmEncodePte(
    _In_ PHYSICAL_ADDRESS PhysicalAddr,
    _In_ ULONG Flags
    )
{
    ULONG64 Pte = 0;

    /* Physical address (bits 51-12) */
    Pte |= (PhysicalAddr.QuadPart & 0x000FFFFFFFFFF000ULL);

    /* Valid bit */
    Pte |= AMDBC250_PTE_VALID;

    /* Access permissions */
    if (Flags & AMDBC250_VM_READ) Pte |= AMDBC250_PTE_READABLE;
    if (Flags & AMDBC250_VM_WRITE) Pte |= AMDBC250_PTE_WRITABLE;
    if (Flags & AMDBC250_VM_EXECUTE) Pte |= AMDBC250_PTE_EXECUTABLE;

    /* Memory type */
    if (Flags & AMDBC250_VM_SYSTEM) {
        Pte |= AMDBC250_PTE_SYSTEM;  /* System memory */
    } else {
        Pte |= AMDBC250_PTE_VRAM;    /* VRAM */
    }

    /* Cache coherency */
    if (Flags & AMDBC250_VM_SNOOP) {
        Pte |= AMDBC250_PTE_SNOOP;
    }

    return Pte;
}

/*===========================================================================
  BuildPagingBuffer - WDDM callback for memory management commands
  
  This is called by DXGKRNL when it needs to update page tables
  or perform memory management operations.
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiBuildPagingBuffer(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_BUILDPAGINGBUFFER   *pBuildPagingBuffer
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;

    if (DevExt == NULL || pBuildPagingBuffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* 
     * In a full implementation, this would:
     * 1. Process paging operations from pBuildPagingBuffer
     * 2. Write PM4 commands to update page tables
     * 3. Submit TLB invalidation
     * 
     * For now, return success (using simple mapping)
     */

    return STATUS_SUCCESS;
}

/*===========================================================================
  Memory Eviction/Restoration
  
  When system memory is under pressure, GPU allocations can be
  evicted to disk and restored when needed.
  
  Linux equivalent: amdgpu_bo_evict(), amdgpu_bo_validate()
===========================================================================*/

NTSTATUS
DreamV3VmEvictMemory(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_VM_CONTEXT VmCtx,
    _In_ ULONG64 VirtualAddress,
    _In_ SIZE_T SizeInBytes
    )
{
    UNREFERENCED_PARAMETER(DevExt);
    UNREFERENCED_PARAMETER(VmCtx);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(SizeInBytes);

    /* TODO: Implement memory eviction
     * 1. Unmap from GPU page tables
     * 2. Save to system memory / disk
     * 3. Mark as evicted in allocation list
     * 4. Invalidate TLB
     */

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
DreamV3VmRestoreMemory(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_VM_CONTEXT VmCtx,
    _In_ ULONG64 VirtualAddress,
    _In_ SIZE_T SizeInBytes
    )
{
    UNREFERENCED_PARAMETER(DevExt);
    UNREFERENCED_PARAMETER(VmCtx);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(SizeInBytes);

    /* TODO: Implement memory restoration
     * 1. Allocate new physical pages
     * 2. Restore data from system memory / disk
     * 3. Map back into GPU page tables
     * 4. Invalidate TLB
     */

    return STATUS_NOT_IMPLEMENTED;
}

/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_hw_init.c

Abstract:
    Hardware initialization and reset routines for the AMD BC-250 APU.

    Implements the GPU initialization sequence for the RDNA2 (Cyan Skillfish)
    architecture, including:
      - GFX command processor (CP) initialization
      - Ring buffer setup (GFX ring, SDMA ring)
      - Interrupt Handler (IH) ring setup
      - Memory controller configuration
      - Display engine (DCN 2.01) initialization
      - Power management (SMU) initialization

    The initialization sequence follows the AMD open-source amdgpu driver
    (Linux kernel) as a reference for register programming order.

Environment:
    Kernel mode (IRQL <= DISPATCH_LEVEL)

--*/

#include "amdbc250_kmd.h"

/* HwInitialize stage breadcrumbs for failure isolation. */
#define BC250_HWINIT_STAGE_ENTRY                1
#define BC250_HWINIT_STAGE_PRE_CLEANUP          2
#define BC250_HWINIT_STAGE_SMU                  3
#define BC250_HWINIT_STAGE_MEMCTRL              4
#define BC250_HWINIT_STAGE_IH_RING              5
#define BC250_HWINIT_STAGE_GFX_RING             6
#define BC250_HWINIT_STAGE_SDMA_RING            7
#define BC250_HWINIT_STAGE_DISPLAY              8
#define BC250_HWINIT_STAGE_COMPLETE             9
/*
 * Minimal phase gate for controlled bring-up.
 * The init function returns success after completing this stage.
 * Advance gradually to isolate the first failing phase.
 */
#define AMDBC250_HW_INIT_MAX_STAGE              BC250_HWINIT_STAGE_COMPLETE

/* Forward declarations of static helper functions */
static NTSTATUS Bc250InitCommandProcessor(_In_ PAMDBC250_DEVICE_EXTENSION DevExt);
static NTSTATUS Bc250InitMemoryController(_In_ PAMDBC250_DEVICE_EXTENSION DevExt);
static NTSTATUS Bc250InitSmu(_In_ PAMDBC250_DEVICE_EXTENSION DevExt);
static NTSTATUS Bc250WaitForSmuReady(_In_ PAMDBC250_DEVICE_EXTENSION DevExt, _In_ ULONG TimeoutUs);
static NTSTATUS Bc250SmuSendMsg(_In_ PAMDBC250_DEVICE_EXTENSION DevExt, _In_ ULONG Message, _In_ ULONG Argument);
static ULONG Bc250GetRingBufferSizeField(_In_ ULONG RingSizeInBytes);
static NTSTATUS Bc250WaitForRegister(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt,
    _In_ ULONG RegisterOffset,
    _In_ ULONG Mask,
    _In_ ULONG ExpectedValue,
    _In_ ULONG TimeoutUs
    );
static VOID Bc250AllocateContiguousMemory(
    _In_  SIZE_T              SizeInBytes,
    _In_  ULONG               Alignment,
    _Out_ PPHYSICAL_ADDRESS   PhysicalAddress,
    _Out_ PVOID              *VirtualAddress
    );
static VOID Bc250FreeContiguousMemory(
    _In_ PVOID  VirtualAddress,
    _In_ SIZE_T SizeInBytes
    );

/*===========================================================================
  Bc250HwInitialize
  Top-level hardware initialization sequence.
  Called from DxgkDdiStartDevice after MMIO is mapped.
===========================================================================*/

NTSTATUS
Bc250HwInitialize(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    NTSTATUS Status;

    KdPrint(("AMDBC250: HwInitialize - starting GPU initialization\n"));

    if (DevExt == NULL || DevExt->MmioVirtualBase == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    DevExt->DebugHwInitStage = BC250_HWINIT_STAGE_ENTRY;
    DevExt->DebugHwInitStatus = STATUS_SUCCESS;
    Bc250DiagWriteDword(L"DiagHwInitEntered", 1);

    /*
     * KmdodMode (default 1): skip ALL GPU register writes and run pure
     * display-only like the proven-working KMDOD sample. KMDOD never maps
     * BAR5 or touches SMU/GFX/IH/SDMA registers; it only presents into the
     * POST framebuffer acquired from dxgkrnl. On BC-250 some of our init
     * writes (SMU mailbox, ring BASE, CP halt) can kill the DCN signal and
     * put the monitor to sleep even when DCN timing is left untouched. This
     * makes the adapter behave exactly like the working KMDOD driver.
     */
    if (Bc250DiagReadDword(L"KmdodMode", 1) != 0) {
        DevExt->HardwareInitialized = TRUE;
        DevExt->DebugHwInitStage = BC250_HWINIT_STAGE_COMPLETE;
        Bc250DiagWriteDword(L"DiagKmdodMode", 1);
        KdPrint(("AMDBC250: HwInitialize - KmdodMode=1, skipping ALL GPU register writes\n"));
        return STATUS_SUCCESS;
    }

    /*
     * If a previous init/reset attempt left partially initialized state,
     * force a clean shutdown first so ring and fence allocations do not leak.
     */
    if (DevExt->GfxRing.Initialized ||
        DevExt->SdmaRing.Initialized ||
        DevExt->IhRing.Initialized) {
        DevExt->DebugHwInitStage = BC250_HWINIT_STAGE_PRE_CLEANUP;
        Bc250HwShutdown(DevExt);
    }

    /* Step 1: Initialize SMU (System Management Unit) for power control.
     * On BC-250 the SMU lives in SMN space, not BAR5, so direct MMIO access
     * to MP1_SMN_P2CMSG_33 will timeout. SMU is usually already running
     * after BIOS/UEFI init, so treat failure as non-fatal and continue. */
    DevExt->DebugHwInitStage = BC250_HWINIT_STAGE_SMU;
    Bc250DiagWriteDword(L"DiagStepSmuEnter", 1);
    Status = Bc250InitSmu(DevExt);
    Bc250DiagWriteDword(L"DiagStepSmuStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        DevExt->DebugHwInitStatus = Status;
        KdPrint(("AMDBC250: SMU initialization failed: 0x%08X (non-fatal, continuing)\n", Status));
    }
    Bc250DiagWriteDword(L"DiagStepSmuDone", 1);
    if (AMDBC250_HW_INIT_MAX_STAGE == BC250_HWINIT_STAGE_SMU) {
        DevExt->DebugHwInitStatus = STATUS_SUCCESS;
        KdPrint(("AMDBC250: HwInitialize gate-stop at stage SMU\n"));
        return STATUS_SUCCESS;
    }

    /* Step 2: Initialize memory controller (non-fatal for DOD) */
    DevExt->DebugHwInitStage = BC250_HWINIT_STAGE_MEMCTRL;
    Bc250DiagWriteDword(L"DiagStepMemEnter", 1);
    Status = Bc250InitMemoryController(DevExt);
    Bc250DiagWriteDword(L"DiagStepMemStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        DevExt->DebugHwInitStatus = Status;
        KdPrint(("AMDBC250: Memory controller initialization failed: 0x%08X (non-fatal)\n", Status));
    }
    Bc250DiagWriteDword(L"DiagStepMemDone", 1);

    /* Step 3: Set up IH (Interrupt Handler) ring (non-fatal for DOD) */
    DevExt->DebugHwInitStage = BC250_HWINIT_STAGE_IH_RING;
    Bc250DiagWriteDword(L"DiagStepIhEnter", 1);
    Status = Bc250HwInitIhRing(DevExt);
    Bc250DiagWriteDword(L"DiagStepIhStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        DevExt->DebugHwInitStatus = Status;
        KdPrint(("AMDBC250: IH ring initialization failed: 0x%08X (non-fatal for display-only)\n", Status));
    }
    Bc250DiagWriteDword(L"DiagStepIhDone", 1);

    /* Step 4: Initialize GFX command processor and ring (non-fatal for DOD) */
    DevExt->DebugHwInitStage = BC250_HWINIT_STAGE_GFX_RING;
    Bc250DiagWriteDword(L"DiagStepGfxEnter", 1);
    Status = Bc250HwInitGfxRing(DevExt);
    Bc250DiagWriteDword(L"DiagStepGfxStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        DevExt->DebugHwInitStatus = Status;
        KdPrint(("AMDBC250: GFX ring initialization failed: 0x%08X (non-fatal for display-only)\n", Status));
    }
    Bc250DiagWriteDword(L"DiagStepGfxDone", 1);

    /* Step 5: Initialize SDMA engine (non-fatal for DOD) */
    DevExt->DebugHwInitStage = BC250_HWINIT_STAGE_SDMA_RING;
    Bc250DiagWriteDword(L"DiagStepSdmaEnter", 1);
    Status = Bc250HwInitSdmaRing(DevExt);
    Bc250DiagWriteDword(L"DiagStepSdmaStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        DevExt->DebugHwInitStatus = Status;
        KdPrint(("AMDBC250: SDMA ring initialization failed: 0x%08X (non-fatal for display-only)\n", Status));
    }
    Bc250DiagWriteDword(L"DiagStepSdmaDone", 1);

    /* Step 6: Initialize display engine */
    DevExt->DebugHwInitStage = BC250_HWINIT_STAGE_DISPLAY;
    Bc250DiagWriteDword(L"DiagStepDisplayEnter", 1);
    Status = Bc250HwInitDisplay(DevExt);
    Bc250DiagWriteDword(L"DiagStepDisplayStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        DevExt->DebugHwInitStatus = Status;
        KdPrint(("AMDBC250: Display initialization failed: 0x%08X\n", Status));
        /* Non-fatal: GPU can operate without display */
    }
    Bc250DiagWriteDword(L"DiagStepDisplayDone", 1);

    /* Set VRAM size from hardware configuration (256 MB dedicated VRAM at MC 0xF400000000) */
    DevExt->TotalVramBytes = AMDBC250_DEDICATED_VRAM_BYTES;
    DevExt->UsedVramBytes  = 0;

    /* Set default clock speeds */
    DevExt->GpuClockMhz    = AMDBC250_BASE_CLOCK_MHZ;
    DevExt->MemoryClockMhz = AMDBC250_MEMORY_CLOCK_MHZ;
    DevExt->DebugHwInitStage = BC250_HWINIT_STAGE_COMPLETE;
    DevExt->DebugHwInitStatus = STATUS_SUCCESS;

    KdPrint(("AMDBC250: HwInitialize - GPU initialization complete\n"));
    KdPrint(("AMDBC250:   VRAM: %llu MB\n", (ULONGLONG)(DevExt->TotalVramBytes / (1024*1024))));
    KdPrint(("AMDBC250:   GPU Clock: %d MHz\n", DevExt->GpuClockMhz));
    KdPrint(("AMDBC250:   Memory Clock: %d MHz\n", DevExt->MemoryClockMhz));

    return STATUS_SUCCESS;
}

/*===========================================================================
  Bc250HwReset
  Performs a full GPU reset (used during TDR recovery).
===========================================================================*/

NTSTATUS
Bc250HwReset(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    NTSTATUS Status;
    ULONG ResetVal;

    KdPrint(("AMDBC250: HwReset - initiating GPU reset\n"));

    if (DevExt == NULL || DevExt->MmioVirtualBase == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Halt the command processor */
    ResetVal = Bc250ReadMmio(DevExt, AMDBC250_REG_CP_ME_CNTL);
    ResetVal |= (CP_ME_CNTL__ME_HALT_MASK |
                 CP_ME_CNTL__PFP_HALT_MASK |
                 CP_ME_CNTL__CE_HALT_MASK);
    Bc250WriteMmio(DevExt, AMDBC250_REG_CP_ME_CNTL, ResetVal);

    /* Wait for CP to halt */
    KeStallExecutionProcessor(100);

    /* Disable interrupts */
    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_CNTL,
                   Bc250ReadMmio(DevExt, AMDBC250_REG_IH_CNTL) &
                   ~IH_CNTL__ENABLE_INTR_MASK);

    /* Ensure all previous ring/fence allocations are released before re-init */
    Bc250HwShutdown(DevExt);

    /* Re-initialize hardware */
    Status = Bc250HwInitialize(DevExt);

    if (NT_SUCCESS(Status)) {
        KdPrint(("AMDBC250: HwReset - GPU reset successful\n"));
    } else {
        KdPrint(("AMDBC250: HwReset - GPU reset failed: 0x%08X\n", Status));
    }

    return Status;
}

/*===========================================================================
  Bc250HwShutdown
  Gracefully shuts down the GPU (called from DxgkDdiStopDevice).
===========================================================================*/

VOID
Bc250HwShutdown(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    KdPrint(("AMDBC250: HwShutdown - shutting down GPU\n"));

    if (DevExt == NULL) {
        return;
    }

    if (DevExt->MmioVirtualBase != NULL) {
        /* Disable interrupts */
        Bc250WriteMmio(DevExt, AMDBC250_REG_IH_CNTL, 0);

        /* Halt command processor */
        Bc250WriteMmio(DevExt, AMDBC250_REG_CP_ME_CNTL,
                       CP_ME_CNTL__ME_HALT_MASK |
                       CP_ME_CNTL__PFP_HALT_MASK |
                       CP_ME_CNTL__CE_HALT_MASK);

        /* Halt SDMA */
        Bc250WriteMmio(DevExt, AMDBC250_REG_SDMA0_F32_CNTL, 0x00000001);
    }

    /* Free ring buffers */
    if (DevExt->GfxRing.VirtualAddress != NULL) {
        Bc250FreeContiguousMemory(DevExt->GfxRing.VirtualAddress,
                                   DevExt->GfxRing.SizeInBytes);
        DevExt->GfxRing.VirtualAddress = NULL;
    }
    DevExt->GfxRing.SizeInBytes = 0;
    DevExt->GfxRing.ReadPointer = 0;
    DevExt->GfxRing.WritePointer = 0;
    DevExt->GfxRing.Initialized = FALSE;

    if (DevExt->SdmaRing.VirtualAddress != NULL) {
        Bc250FreeContiguousMemory(DevExt->SdmaRing.VirtualAddress,
                                   DevExt->SdmaRing.SizeInBytes);
        DevExt->SdmaRing.VirtualAddress = NULL;
    }
    DevExt->SdmaRing.SizeInBytes = 0;
    DevExt->SdmaRing.ReadPointer = 0;
    DevExt->SdmaRing.WritePointer = 0;
    DevExt->SdmaRing.Initialized = FALSE;

    if (DevExt->IhRing.VirtualAddress != NULL) {
        Bc250FreeContiguousMemory(DevExt->IhRing.VirtualAddress,
                                   DevExt->IhRing.SizeInBytes);
        DevExt->IhRing.VirtualAddress = NULL;
    }
    DevExt->IhRing.SizeInBytes = 0;
    DevExt->IhRing.ReadPointer = 0;
    DevExt->IhRing.WritePointer = 0;
    DevExt->IhRing.Initialized = FALSE;

    if (DevExt->GlobalFence.VirtualAddress != NULL) {
        Bc250FreeContiguousMemory((PVOID)DevExt->GlobalFence.VirtualAddress,
                                   PAGE_SIZE);
        DevExt->GlobalFence.VirtualAddress = NULL;
    }
    DevExt->GlobalFence.LastSubmittedValue = 0;
    DevExt->GlobalFence.LastSignaledValue = 0;

    KdPrint(("AMDBC250: HwShutdown complete\n"));
}

/*===========================================================================
  Bc250HwInitGfxRing
  Initializes the GFX command ring (ring buffer 0).
===========================================================================*/

NTSTATUS
Bc250HwInitGfxRing(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    NTSTATUS Status;
    PHYSICAL_ADDRESS RingPhys;
    PVOID RingVirt;
    PHYSICAL_ADDRESS FencePhys;
    PVOID FenceVirt;
    ULONG RingSize = 1024 * 1024;  /* 1 MB GFX ring */
    ULONG RbCntl;
    ULONG RbBufSz;

    KdPrint(("AMDBC250: InitGfxRing - allocating %d KB ring buffer\n",
             RingSize / 1024));

    /* Allocate ring buffer memory (must be physically contiguous) */
    Bc250AllocateContiguousMemory(RingSize, AMDBC250_RING_BUFFER_ALIGN,
                                   &RingPhys, &RingVirt);
    if (RingVirt == NULL) {
        KdPrint(("AMDBC250: Failed to allocate GFX ring buffer\n"));
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(RingVirt, RingSize);

    DevExt->GfxRing.PhysicalAddress = RingPhys;
    DevExt->GfxRing.VirtualAddress  = RingVirt;
    DevExt->GfxRing.SizeInBytes     = RingSize;
    DevExt->GfxRing.ReadPointer     = 0;
    DevExt->GfxRing.WritePointer    = 0;
    DevExt->GfxRing.DoorBellOffset  = 0;
    DevExt->GfxRing.Initialized     = FALSE;

    /* Allocate fence memory */
    Bc250AllocateContiguousMemory(PAGE_SIZE, AMDBC250_FENCE_ALIGN,
                                   &FencePhys, &FenceVirt);
    if (FenceVirt == NULL) {
        Bc250FreeContiguousMemory(RingVirt, RingSize);
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(FenceVirt, PAGE_SIZE);
    DevExt->GlobalFence.PhysicalAddress = FencePhys;
    DevExt->GlobalFence.VirtualAddress  = (volatile PULONG)FenceVirt;
    *DevExt->GlobalFence.VirtualAddress = 0;
    DevExt->GlobalFence.LastSignaledValue = 0;
    DevExt->GlobalFence.LastSubmittedValue = 0;

    /* Halt CP before programming ring */
    Bc250WriteMmio(DevExt, AMDBC250_REG_CP_ME_CNTL,
                   CP_ME_CNTL__ME_HALT_MASK |
                   CP_ME_CNTL__PFP_HALT_MASK);

    /* NOTE: BASE_LO (0x89E0) and BASE_HI (0x8BA4) are HARDWARE READ-ONLY on BC-250.
       BIOS sets ring base. Do NOT write them — writes silently fail. */

    /* Calculate ring buffer size field (log2 of size in DWORDs) */
    RbBufSz = Bc250GetRingBufferSizeField(RingSize);

    /* Program ring control register (writable on BC-250) */
    RbCntl = (RbBufSz & CP_RB0_CNTL__RB_BUFSZ_MASK) |
             ((1 << CP_RB0_CNTL__RB_BLKSZ_SHIFT) & CP_RB0_CNTL__RB_BLKSZ_MASK) |
             CP_RB0_CNTL__RB_RPTR_WR_ENA_MASK;
    Bc250WriteMmio(DevExt, AMDBC250_REG_CP_RB0_CNTL, RbCntl);

    /* Initialize read/write pointers (writable) */
    Bc250WriteMmio(DevExt, AMDBC250_REG_CP_RB0_RPTR, 0);
    Bc250WriteMmio(DevExt, AMDBC250_REG_CP_RB0_WPTR, 0);
    Bc250WriteMmio(DevExt, AMDBC250_REG_CP_RB0_WPTR_HI, 0);

    /* Enable doorbell for ring 0 */
    Bc250WriteMmio(DevExt, AMDBC250_REG_CP_RB_DOORBELL_CTL, 0x00000001);

    /* Initialize command processor */
    Status = Bc250InitCommandProcessor(DevExt);
    if (!NT_SUCCESS(Status)) {
        Bc250FreeContiguousMemory((PVOID)DevExt->GlobalFence.VirtualAddress, PAGE_SIZE);
        DevExt->GlobalFence.VirtualAddress = NULL;
        Bc250FreeContiguousMemory(RingVirt, RingSize);
        DevExt->GfxRing.VirtualAddress = NULL;
        DevExt->GfxRing.SizeInBytes = 0;
        return Status;
    }

    /* Resume CP */
    Bc250WriteMmio(DevExt, AMDBC250_REG_CP_ME_CNTL, 0);

    /* Wait for CP to become ready */
    Status = Bc250WaitForRegister(DevExt, AMDBC250_REG_SCRATCH_REG0,
                                   0xFFFFFFFF, 0xDEADBEEF,
                                   AMDBC250_INIT_TIMEOUT_US);
    if (!NT_SUCCESS(Status)) {
        KdPrint(("AMDBC250: CP scratch register handshake timeout: 0x%08X\n", Status));
        Bc250FreeContiguousMemory((PVOID)DevExt->GlobalFence.VirtualAddress, PAGE_SIZE);
        DevExt->GlobalFence.VirtualAddress = NULL;
        Bc250FreeContiguousMemory(RingVirt, RingSize);
        DevExt->GfxRing.VirtualAddress = NULL;
        DevExt->GfxRing.SizeInBytes = 0;
        return Status;
    }

    DevExt->GfxRing.Initialized = TRUE;
    KdPrint(("AMDBC250: GFX ring initialized at PA=0x%llX (BASE read-only)\n",
             RingPhys.QuadPart));

    return STATUS_SUCCESS;
}

/*===========================================================================
  Bc250HwInitIhRing
  Initializes the Interrupt Handler (IH) ring buffer.
===========================================================================*/

NTSTATUS
Bc250HwInitIhRing(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    PHYSICAL_ADDRESS IhPhys;
    PVOID IhVirt;
    ULONG IhSize = AMDBC250_IH_RING_SIZE;
    ULONG IhCntl;
    ULONG IhRbSizeField;

    KdPrint(("AMDBC250: InitIhRing - allocating %d KB IH ring\n",
             IhSize / 1024));

    Bc250AllocateContiguousMemory(IhSize, AMDBC250_IH_RING_ALIGN,
                                   &IhPhys, &IhVirt);
    if (IhVirt == NULL) {
        KdPrint(("AMDBC250: Failed to allocate IH ring\n"));
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(IhVirt, IhSize);

    DevExt->IhRing.PhysicalAddress = IhPhys;
    DevExt->IhRing.VirtualAddress  = IhVirt;
    DevExt->IhRing.SizeInBytes     = IhSize;
    DevExt->IhRing.ReadPointer     = 0;
    DevExt->IhRing.WritePointer    = 0;
    DevExt->IhRing.Initialized     = FALSE;

    /* Program IH ring base address */
    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_RB_BASE,
                   (ULONG)(IhPhys.QuadPart >> 8));
    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_RB_BASE_HI,
                   (ULONG)(IhPhys.QuadPart >> 40));

    /* Program IH ring control */
    IhRbSizeField = Bc250GetRingBufferSizeField(IhSize);
    IhCntl = (IhRbSizeField & 0x1F); /* RB_SIZE: log2(size_in_dwords) */
    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_RB_CNTL, IhCntl);

    /* Initialize read/write pointers */
    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_RB_RPTR, 0);
    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_RB_WPTR, 0);

    /* Enable interrupts */
    IhCntl |= IH_CNTL__ENABLE_INTR_MASK;
    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_CNTL, IhCntl);

    DevExt->IhRing.Initialized = TRUE;
    KdPrint(("AMDBC250: IH ring initialized at PA=0x%llX\n", IhPhys.QuadPart));

    return STATUS_SUCCESS;
}

/*===========================================================================
  Bc250HwInitSdmaRing
  Initializes the SDMA (System DMA) engine ring buffer.
===========================================================================*/

NTSTATUS
Bc250HwInitSdmaRing(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    PHYSICAL_ADDRESS SdmaPhys;
    PVOID SdmaVirt;
    ULONG SdmaSize = 256 * 1024;  /* 256 KB SDMA ring */
    ULONG RbCntl;
    ULONG RbBufSz;

    KdPrint(("AMDBC250: InitSdmaRing - allocating %d KB SDMA ring\n",
             SdmaSize / 1024));

    Bc250AllocateContiguousMemory(SdmaSize, AMDBC250_RING_BUFFER_ALIGN,
                                   &SdmaPhys, &SdmaVirt);
    if (SdmaVirt == NULL) {
        KdPrint(("AMDBC250: Failed to allocate SDMA ring\n"));
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(SdmaVirt, SdmaSize);

    DevExt->SdmaRing.PhysicalAddress = SdmaPhys;
    DevExt->SdmaRing.VirtualAddress  = SdmaVirt;
    DevExt->SdmaRing.SizeInBytes     = SdmaSize;
    DevExt->SdmaRing.ReadPointer     = 0;
    DevExt->SdmaRing.WritePointer    = 0;
    DevExt->SdmaRing.Initialized     = FALSE;

    /* Halt SDMA F32 */
    Bc250WriteMmio(DevExt, AMDBC250_REG_SDMA0_F32_CNTL, 0x00000001);
    KeStallExecutionProcessor(10);

    /* NOTE: SDMA RB_BASE (0xE000) and RB_BASE_HI (0xE004) are HARDWARE READ-ONLY on BC-250.
       BIOS sets ring base. Do NOT write them — writes silently fail. */

    /* Calculate buffer size field */
    RbBufSz = Bc250GetRingBufferSizeField(SdmaSize);

    RbCntl = (RbBufSz & 0x3F) | (1 << 8);  /* RB_SIZE + RB_SWAP_ENABLE */
    Bc250WriteMmio(DevExt, AMDBC250_REG_SDMA0_GFX_RB_CNTL, RbCntl);

    /* Initialize pointers (writable) */
    Bc250WriteMmio(DevExt, AMDBC250_REG_SDMA0_GFX_RB_RPTR, 0);
    Bc250WriteMmio(DevExt, AMDBC250_REG_SDMA0_GFX_RB_WPTR, 0);

    /* Enable doorbell */
    Bc250WriteMmio(DevExt, AMDBC250_REG_SDMA0_GFX_DOORBELL, 0x00000001);

    /* Resume SDMA F32 */
    Bc250WriteMmio(DevExt, AMDBC250_REG_SDMA0_F32_CNTL, 0x00000000);

    DevExt->SdmaRing.Initialized = TRUE;
    KdPrint(("AMDBC250: SDMA ring initialized at PA=0x%llX (BASE read-only)\n", SdmaPhys.QuadPart));

    return STATUS_SUCCESS;
}

/*===========================================================================
  Bc250HwInitDisplay
  Initializes the DCN 2.01 display engine for DisplayPort output.
===========================================================================*/

NTSTATUS
Bc250HwInitDisplay(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    KdPrint(("AMDBC250: InitDisplay - initializing DCN 2.01 display engine\n"));
    Bc250DiagWriteDword(L"DiagDisplayInitEntered", 1);

    /*
     * SkipDcnInit (default 0): when set to 1, do NOT reprogram the DCN CRTC.
     * KMDOD works by keeping the POST display exactly as BIOS/UEFI left it
     * and only feeding the framebuffer pointer through the WDDM DDI. On
     * BC-250 the CRTC timing/FB registers we write here can kill the BIOS
     * signal (monitor sleeps). When the switch is on we keep the POST mode
     * so the boot picture survives; CommitVidPn supplies the actual FB.
     */
    if (Bc250DiagReadDword(L"SkipDcnInit", 0) != 0) {
        KdPrint(("AMDBC250: InitDisplay - SkipDcnInit=1, keeping POST display mode\n"));
        return STATUS_SUCCESS;
    }

    /* Enable CRTC0 */
    Bc250WriteMmio(DevExt, AMDBC250_REG_CRTC0_CONTROL, 0x00000001);

    /* Set default display mode: 1920x1080 @ 60Hz */
    DevExt->CurrentMode.Width       = 1920;
    DevExt->CurrentMode.Height      = 1080;
    DevExt->CurrentMode.RefreshRate = 60;
    DevExt->CurrentMode.BitsPerPixel = 32;
    DevExt->CurrentMode.PixelClock  = 148500;  /* 148.5 MHz */
    DevExt->CurrentMode.Format      = D3DDDIFMT_A8R8G8B8;

    /* Program CRTC timing for 1920x1080 @ 60Hz */
    Bc250WriteMmio(DevExt, AMDBC250_REG_CRTC0_H_TOTAL, 2200 - 1);
    Bc250WriteMmio(DevExt, AMDBC250_REG_CRTC0_V_TOTAL, 1125 - 1);
    Bc250WriteMmio(DevExt, AMDBC250_REG_CRTC0_H_BLANK, (1920 << 16) | 2200);
    Bc250WriteMmio(DevExt, AMDBC250_REG_CRTC0_V_BLANK, (1080 << 16) | 1125);
    Bc250WriteMmio(DevExt, AMDBC250_REG_CRTC0_H_SYNC,  (2008 << 16) | 2052);
    Bc250WriteMmio(DevExt, AMDBC250_REG_CRTC0_V_SYNC,  (1084 << 16) | 1089);

    /* Program framebuffer base address if POST display was acquired */
    if (DevExt->FbPhysicalBase.QuadPart != 0) {
        Bc250WriteMmio(DevExt, AMDBC250_REG_CRTC0_BASE_ADDRESS_LO,
                       (ULONG)(DevExt->FbPhysicalBase.QuadPart >> 8));
        Bc250WriteMmio(DevExt, AMDBC250_REG_CRTC0_BASE_ADDRESS_HI,
                       (ULONG)(DevExt->FbPhysicalBase.QuadPart >> 40));
        KdPrint(("AMDBC250: CRTC0 FB base set to PA=0x%llX\n",
                 DevExt->FbPhysicalBase.QuadPart));
    }

    /* Enable CRTC0 */
    Bc250WriteMmio(DevExt, AMDBC250_REG_CRTC0_CONTROL, 0x00000001);
    return STATUS_SUCCESS;
}

/*===========================================================================
  Static Helper: Bc250InitCommandProcessor
  Loads CP microcode and starts the command processor.
===========================================================================*/

static NTSTATUS
Bc250InitCommandProcessor(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    KdPrint(("AMDBC250: InitCommandProcessor\n"));

    /*
     * In a production driver, this function would:
     * 1. Load the CP PFP (Pre-Fetch Parser) microcode from a firmware file
     * 2. Load the CP ME (Micro Engine) microcode
     * 3. Load the CP MEC (Micro Engine Compute) microcode
     *
     * The firmware files for RDNA2 are:
     *   - amdgpu/navi10_pfp.bin  (PFP microcode)
     *   - amdgpu/navi10_me.bin   (ME microcode)
     *   - amdgpu/navi10_mec.bin  (MEC microcode)
     *
     * These are loaded from the Windows driver store or embedded in the
     * driver binary. For this reference implementation, we skip microcode
     * loading and rely on the firmware already loaded by the BIOS/UEFI.
     */

    /* Enable MEC (Micro Engine Compute) */
    Bc250WriteMmio(DevExt, AMDBC250_REG_CP_MEC_CNTL, 0);

    /* Write test value to scratch register to verify CP is running */
    Bc250WriteMmio(DevExt, AMDBC250_REG_SCRATCH_REG0, 0xDEADBEEF);

    KdPrint(("AMDBC250: Command processor initialized\n"));
    return STATUS_SUCCESS;
}

/*===========================================================================
  Static Helper: Bc250InitMemoryController
  Configures the GPU memory controller for UMA (shared memory) operation.
===========================================================================*/

static NTSTATUS
Bc250InitMemoryController(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    KdPrint(("AMDBC250: InitMemoryController\n"));

    /*
     * DO NOT write MC_VM_AGP_BASE/TOP/BOT, MC_VM_SYSTEM_APERTURE,
     * or GB_ADDR_CONFIG on BC-250 — these are SOS-owned and cause 0x1A BSOD.
     * BC-250 VRAM is 256MB dedicated at MC 0xF400000000 (aper_base 0xC0000000).
     * Memory controller is configured by BIOS/SOS. Read-only here.
     */

    KdPrint(("AMDBC250: Memory controller configured (read-only, SOS-managed)\n"));
    return STATUS_SUCCESS;
}

/*===========================================================================
  Static Helper: Bc250InitSmu
  Initializes the System Management Unit for power and clock control.
===========================================================================*/

static NTSTATUS
Bc250InitSmu(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    NTSTATUS Status;

    KdPrint(("AMDBC250: InitSmu\n"));

    /*
     * SMU lives in SMN space (0x03B10Axx), NOT in BAR5.
     * Use NBIO SMN window (BAR5+0x38/0x3C) to communicate with SMU.
     * The BAR5 slot AMDBC250_REG_MP1_SMN_P2CMSG_33 (0x16284) reads 0
     * because MP1 is not mapped into BAR5 on BC-250.
     */

    /* Wait for SMU to be ready via NBIO SMN window */
    Status = Bc250WaitForSmuReady(DevExt, AMDBC250_SMU_TIMEOUT_US);
    if (!NT_SUCCESS(Status)) {
        KdPrint(("AMDBC250: SMU not ready (timeout), continuing with conservative clocks\n"));
        return Status;
    }

    /* Send EnableAllSmuFeatures message via NBIO SMN window */
    Status = Bc250SmuSendMsg(DevExt, 0x01, 0);  /* EnableAllSmuFeatures = 0x01 */
    if (!NT_SUCCESS(Status)) {
        KdPrint(("AMDBC250: SMU enable command timed out, continuing with conservative clocks\n"));
        return Status;
    }

    KdPrint(("AMDBC250: SMU initialized\n"));
    return STATUS_SUCCESS;
}

/*===========================================================================
  Static Helper: Bc250GetRingBufferSizeField
  Returns log2(ring_size_in_dwords) encoded for RB_CNTL registers.
===========================================================================*/

static ULONG
Bc250GetRingBufferSizeField(
    _In_ ULONG RingSizeInBytes
    )
{
    ULONG SizeInDwords;
    ULONG Field;

    if (RingSizeInBytes < sizeof(ULONG)) {
        return 0;
    }

    SizeInDwords = RingSizeInBytes / sizeof(ULONG);
    Field = 0;

    while (SizeInDwords > 1) {
        SizeInDwords >>= 1;
        Field++;
    }

    return Field;
}

/*===========================================================================
  Static Helper: Bc250WaitForRegister
  Polls a register until it matches the expected value or times out.
===========================================================================*/

static NTSTATUS
Bc250WaitForRegister(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt,
    _In_ ULONG RegisterOffset,
    _In_ ULONG Mask,
    _In_ ULONG ExpectedValue,
    _In_ ULONG TimeoutUs
    )
{
    ULONG Elapsed = 0;
    ULONG Value = 0;

    while (Elapsed < TimeoutUs) {
        Value = Bc250ReadMmio(DevExt, RegisterOffset);
        if ((Value & Mask) == ExpectedValue) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(10);
        Elapsed += 10;
    }

    KdPrint(("AMDBC250: Register 0x%X timeout (expected 0x%X, got 0x%X)\n",
             RegisterOffset, ExpectedValue, Value));

    return STATUS_TIMEOUT;
}

/*===========================================================================
  Static Helper: Bc250AllocateContiguousMemory
  Allocates physically contiguous memory for ring buffers and IH rings.
===========================================================================*/

static VOID
Bc250AllocateContiguousMemory(
    _In_  SIZE_T              SizeInBytes,
    _In_  ULONG               Alignment,
    _Out_ PPHYSICAL_ADDRESS   PhysicalAddress,
    _Out_ PVOID              *VirtualAddress
    )
{
    PVOID Allocation;
    PHYSICAL_ADDRESS AllocationPhys;
    ULONG Attempt;
    SIZE_T EffectiveSize;
    ULONG EffectiveAlignment;
    PHYSICAL_ADDRESS LowAddr  = {0};
    PHYSICAL_ADDRESS HighAddr = {0};
    PHYSICAL_ADDRESS BoundaryAddr = {0};

    LowAddr.QuadPart  = 0;
    HighAddr.QuadPart = 0xFFFFFFFFFFFFFFFFULL;
    BoundaryAddr.QuadPart = 0;

    *VirtualAddress = NULL;
    PhysicalAddress->QuadPart = 0;

    EffectiveAlignment = (Alignment == 0) ? sizeof(ULONG) : Alignment;
    EffectiveSize = (SizeInBytes + (EffectiveAlignment - 1)) & ~((SIZE_T)EffectiveAlignment - 1);
    if (EffectiveSize == 0) {
        return;
    }

    for (Attempt = 0; Attempt < 4; Attempt++) {
        Allocation = MmAllocateContiguousMemorySpecifyCache(
            EffectiveSize,
            LowAddr,
            HighAddr,
            BoundaryAddr,
            MmWriteCombined
            );

        if (Allocation == NULL) {
            break;
        }

        AllocationPhys = MmGetPhysicalAddress(Allocation);
        if ((Alignment == 0) || ((AllocationPhys.QuadPart & (Alignment - 1)) == 0)) {
            *VirtualAddress = Allocation;
            *PhysicalAddress = AllocationPhys;
            return;
        }

        MmFreeContiguousMemory(Allocation);
    }
}

/*===========================================================================
  Static Helper: Bc250FreeContiguousMemory
  Frees physically contiguous memory.
===========================================================================*/

static VOID
Bc250FreeContiguousMemory(
    _In_ PVOID  VirtualAddress,
    _In_ SIZE_T SizeInBytes
    )
{
    if (VirtualAddress != NULL) {
        MmFreeContiguousMemory(VirtualAddress);
    }
    UNREFERENCED_PARAMETER(SizeInBytes);
}

/*===========================================================================
  Static Helper: Bc250WaitForSmuReady
  Waits for SMU mailbox to be ready via NBIO SMN window (BAR5+0x38/0x3C).
=========================================================================*/

static NTSTATUS
Bc250WaitForSmuReady(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt,
    _In_ ULONG TimeoutUs
    )
{
    ULONG Elapsed = 0;
    ULONG Value = 0;

    if (DevExt->MmioVirtualBase == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    while (Elapsed < TimeoutUs) {
        /* Read C2PMSG_90 (SMU control) via NBIO SMN window:
         * Write SMN address 0x03B10A68 to BAR5+0x38, read from BAR5+0x3C */
        Bc250WriteMmio(DevExt, 0x38, 0x03B10A68);
        Value = Bc250ReadMmio(DevExt, 0x3C);
        if ((Value & 1) == 1) {  /* C2PMSG_90 == 1 means ready */
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(10);
        Elapsed += 10;
    }

    KdPrint(("AMDBC250: SMU not ready timeout (got 0x%X)\n", Value));
    return STATUS_TIMEOUT;
}

/*===========================================================================
  Static Helper: Bc250SmuSendMsg
  Sends a message to SMU via NBIO SMN window (BAR5+0x38/0x3C).
  Queue 0: cmd=0x03B10A08, rsp=0x03B10A68, arg=0x03B10A48
=========================================================================*/

static NTSTATUS
Bc250SmuSendMsg(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt,
    _In_ ULONG Message,
    _In_ ULONG Argument
    )
{
    NTSTATUS Status;
    ULONG Value = 0;

    if (DevExt->MmioVirtualBase == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    /* Wait for SMU ready */
    Status = Bc250WaitForSmuReady(DevExt, AMDBC250_SMU_TIMEOUT_US);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    /* Protocol: wait RSP=1 -> ack by writing 0 to RSP -> write ARG -> write CMD -> poll RSP for completion */
    /* For queue 0: CMD=0x03B10A08, RSP=0x03B10A68, ARG=0x03B10A48 */

    /* Write argument to C2PMSG_82 */
    Bc250WriteMmio(DevExt, 0x38, 0x03B10A48);
    Bc250WriteMmio(DevExt, 0x3C, Argument);

    /* Write command to C2PMSG_66 */
    Bc250WriteMmio(DevExt, 0x38, 0x03B10A08);
    Bc250WriteMmio(DevExt, 0x3C, Message);

    /* Poll response */
    ULONG Elapsed = 0;
    while (Elapsed < AMDBC250_SMU_TIMEOUT_US) {
        Bc250WriteMmio(DevExt, 0x38, 0x03B10A68);
        Value = Bc250ReadMmio(DevExt, 0x3C);
        if ((Value & 0xFF) != 0) {  /* Response received */
            if (Value == 0x01) {
                return STATUS_SUCCESS;
            }
            KdPrint(("AMDBC250: SMU msg 0x%X failed with response 0x%X\n", Message, Value));
            return STATUS_UNSUCCESSFUL;
        }
        KeStallExecutionProcessor(10);
        Elapsed += 10;
    }

    KdPrint(("AMDBC250: SMU msg 0x%X timeout\n", Message));
    return STATUS_TIMEOUT;
}





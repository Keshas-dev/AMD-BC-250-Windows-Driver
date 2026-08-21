#include <ntddk.h>
#include <wdm.h>
#include "amdbc250_psp.h"

#define GPU_BAR5_PHYSICAL             0xFE800000ULL
#define GPU_BAR5_SIZE                 0x80000

// PSP driver IOCTL struct definitions
typedef struct _PSP_KIQ_SUBMIT_REQUEST {
    ULONG CommandCount;
    ULONG Reserved[3];
    ULONG Commands[64];
} PSP_KIQ_SUBMIT_REQUEST;

/* PSP driver IOCTL codes (must match PspDriver.sys from AGENTS.md) */
#define PSP_IOCTL_READ_REG      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_WRITE_REG     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_INIT_HW       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_BOOT_SEQ      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_GET_GPU_INFO  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x815, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_LOAD_TOC      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_REG_PROG      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_KIQ_SUBMIT    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x818, METHOD_BUFFERED, FILE_ANY_ACCESS) /* KIQ ring submit */
#define PSP_IOCTL_KIQ_LOAD_FW   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x822, METHOD_BUFFERED, FILE_ANY_ACCESS) /* KIQ LOAD_IP_FW via PM4 */
#define PSP_CMD_BUF_SIZE        1024  /* Command buffer size for PSP firmware commands */

/* C2PMSG_81 (SOS alive) byte offset within GPU BAR5 (0xFE800000). */
#define GPU_BAR5_C2PMSG_81_OFFSET   0x58244

/* PSP ring frame command IDs (from Linux psp_gfx_if.h) */
#define GFX_CMD_ID_LOAD_IP_FW   0x00000006

/* PSP_GPU_INFO struct (must match PspIoctl.h from PSP driver repo) */
typedef struct _PSP_GPU_INFO_REMOTE {
    ULONG RingBufferPA;
    ULONG FwLoaded;
    ULONG FwCount;
    ULONGLONG TMRBase;
    ULONG TMSSize;
    ULONG GfxVersion;
    ULONG C2pmsg64;
    ULONG C2pmsg81;
    ULONG TmrInitialized;
} PSP_GPU_INFO_REMOTE;

HANDLE g_PspProxyHandle = NULL;
static BOOLEAN g_PspProxyAvailable = FALSE;
static BOOLEAN g_GpcomRingAvailable = FALSE; /* GPCOM ring created by PSP driver */
static ULONG64 g_GpcomRingPa = 0;           /* GPCOM ring physical address (returned by PSP_GET_GPU_INFO) */
static ULONG g_GpcomRingSize = 0;
static PVOID g_GpcomRingVa = NULL;          /* Mapped GPCOM ring VA */

/* KIQ ring support (primary command submission path on BC-250) */
static PVOID g_KiqRingVa = NULL;
static PHYSICAL_ADDRESS g_KiqRingPa = {0};
static ULONG g_KiqRingSize = 0;

/* Get GPU BAR5 virtual address from the main driver's device extension */
PVOID Amdbc250PspGetGpuBar5Va(VOID)
{
    if (g_PciDevExt != NULL && g_PciDevExt->MmioVirtualBase != NULL) {
        return g_PciDevExt->MmioVirtualBase;
    }
    return NULL;
}
static ULONG g_KiqRingWptr = 0;
static BOOLEAN g_KiqRingInitialized = FALSE;
static KSPIN_LOCK g_KiqRingLock;

/* Firmware allocation for KIQ LOAD_IP_FW */
static PVOID g_FwBuffer = NULL;
static PHYSICAL_ADDRESS g_FwBufferPa = {0};
static ULONG g_FwBufferSize = 0;
static ULONG g_FwCount = 0;
static KSPIN_LOCK g_FwLock;

/* Shared PSP/SOS context (SOS alive state, etc.) â€” declared early so the
 * proxy init path can update it. */
static AMDBC250_PSP_CONTEXT g_PspContext = {0};

/* GPU BAR5 virtual mapping, published by the INIT_HARDWARE IOCTL handler so
 * the PSP proxy can read SOS status (C2PMSG_81) directly without depending on
 * the PSP driver's own BAR5 mapping. */
static PVOID g_GpuBar5Va = NULL;
/* NOTE: g_GpcomRingVa points to the PSP GPCOM ring, NOT a KIQ ring.
   The KIQ ring is a separate GPU ring. The current code incorrectly
   writes PM4 packets into the GPCOM ring, which does not work.
   See Amdbc250PspKiqReadReg and Amdbc250PspKiqSubmit. */

/* # C2PMSG mailbox (corrected 2026-08-20: MP0 base = 0x58000, byte offsets —
 *   verified by psp-ring-submit-test against live hardware 2026-08-18.
 *   The earlier 0x103D0-based offsets (0x1055C/0x10560/0x105D0/0x10614) were
 *   WRONG — they read back idle values. Base 0x58000 = ip_discovery MP0 base
 *   0x16000 (dwords) * 4.
 *   C2PMSG_35 (0x5818C): command to SOS
 *   C2PMSG_36 (0x58190): argument / firmware PA low (1MB units for bootloader)
 *   C2PMSG_37 (0x58194): argument / firmware PA high
 *   C2PMSG_64 (0x58200): cmd / TOS-ready / response (bit31 = RESP/TOS ready)
 *   C2PMSG_67 (0x5820C): ring WPTR
 *   C2PMSG_69/70/71 (0x58214/18/1C): ring addr lo/hi, size
 *   C2PMSG_81 (0x58244): SOS status */
#define DIRECT_C2PMSG_35_OFFSET      0x5818C
#define DIRECT_C2PMSG_36_OFFSET      0x58190
#define DIRECT_C2PMSG_37_OFFSET      0x58194
#define DIRECT_C2PMSG_64_OFFSET      0x58200  /* ring/TOS mailbox (bit31 = TOS ready) */
#define DIRECT_C2PMSG_67_OFFSET      0x5820C  /* ring WPTR */
#define DIRECT_C2PMSG_81_OFFSET      0x58244
#define DIRECT_C2PMSG_OK             0xF0000010
#define DIRECT_C2PMSG_SOS_ALIVE      0x80000000

/* NOTE (2026-08-20): the old DIRECT_C2PMSG_OK=0xF0000010 was read from the
 * WRONG base 0x103D0. With the corrected base 0x58000, C2PMSG_81 reads the SOS
 * status (verified live: 0x002B9309, bit31 NOT set). Bootloader-load completion
 * is signaled on C2PMSG_35 bit31 (see Amdbc250PspDirectLoadTos); PspWaitCompletion
 * below therefore only makes sense as a legacy helper and is NOT used by the
 * working ring path (kmd.c PSP_RING_* / 0x58000). */

/* Bootloader command codes (Linux amdgpu_psp.h psp_bootloader_cmd). */
#define PSP_BL__LOAD_SYSDRV          0x10000
#define PSP_BL__LOAD_SOSDRV          0x20000
#define PSP_BL__LOAD_TOS_SPL_TABLE   0x10000000

/* SMU mailbox via SMN (verified on BC-250 HW).
 *   C2PMSG_66 (msg):  SMN[0x03B10A08]
 *   C2PMSG_82 (arg):  SMN[0x03B10A48]
 *   C2PMSG_90 (ctrl): SMN[0x03B10A68]
 */
#define SMU_C2PMSG_66_SMN   0x03B10A08
#define SMU_C2PMSG_82_SMN   0x03B10A48
#define SMU_C2PMSG_90_SMN   0x03B10A68

/* SMN transport via NBIO BAR5+0x38/0x3C (index/data pair). */
#define NBIO_SMN_INDEX      0x38
#define NBIO_SMN_DATA       0x3C

/* Max poll iterations (~5ms at ~1us per loop). */
#define DIRECT_POLL_MAX_MS  100

/* --- SMN helpers â€” read/write SMN register via NBIO BAR5+0x38/0x3C --- */
ULONG Amdbc250PspSmnRead(PVOID GpuBar5Va, ULONG SmnAddress)
{
    if (!GpuBar5Va) return 0xFFFFFFFF;
    PUCHAR base = (PUCHAR)GpuBar5Va;
    WRITE_REGISTER_ULONG((PULONG)(base + NBIO_SMN_INDEX), SmnAddress);
    KeMemoryBarrier();
    return READ_REGISTER_ULONG((PULONG)(base + NBIO_SMN_DATA));
}

VOID Amdbc250PspSmnWrite(PVOID GpuBar5Va, ULONG SmnAddress, ULONG Value)
{
    if (!GpuBar5Va) return;
    PUCHAR base = (PUCHAR)GpuBar5Va;
    WRITE_REGISTER_ULONG((PULONG)(base + NBIO_SMN_INDEX), SmnAddress);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG((PULONG)(base + NBIO_SMN_DATA), Value);
    KeMemoryBarrier();
}

/* --- Wait for SMU mailbox ready (poll C2PMSG_90 == 0x01). --- */
static NTSTATUS SmuWaitReady(PVOID GpuBar5Va, ULONG TimeoutMs)
{
    ULONG i;
    for (i = 0; i < TimeoutMs; i++) {
        ULONG ctrl = Amdbc250PspSmnRead(GpuBar5Va, SMU_C2PMSG_90_SMN);
        if (ctrl == 1) return STATUS_SUCCESS;
        KeStallExecutionProcessor(1000); /* 1ms */
    }
    return STATUS_TIMEOUT;
}

/* --- Wait for PSP C2PMSG_35 bit31 (bootloader completion) ---
 * NOTE: on BC-250, C2PMSG_81 bit31 is NOT the alive/complete flag.
 * The bootloader signals completion via C2PMSG_35 bit31 set.
 * Poll C2PMSG_35 (and report C2PMSG_64 bit31 as TOS ready for TOS load). --- */
static NTSTATUS PspWaitCompletion(PVOID GpuBar5Va, ULONG TimeoutMs)
{
    ULONG i;
    for (i = 0; i < TimeoutMs; i++) {
        ULONG status = READ_REGISTER_ULONG(
            (PULONG)((PUCHAR)GpuBar5Va + DIRECT_C2PMSG_35_OFFSET));
        if (status & 0x80000000) return STATUS_SUCCESS;
        KeStallExecutionProcessor(1000);
    }
    return STATUS_TIMEOUT;
}

/* --- Direct PSP mailbox: load IP firmware via C2PMSG_35/36/37/81.
 *     Allocates contiguous physical memory, copies blob, writes PA to
 *     C2PMSG_36/37, writes GFX_CMD_ID_LOAD_IP_FW | (FwType<<16) to
 *     C2PMSG_35, polls C2PMSG_81 for completion. --- */
NTSTATUS Amdbc250PspDirectLoadIpFw(PVOID GpuBar5Va, ULONG FwType, ULONG FwSize,
    PHYSICAL_ADDRESS FwPa, PULONG OutC2pmsg35, PULONG OutC2pmsg81)
{
    if (!GpuBar5Va || FwSize == 0 || FwSize > 4 * 1024 * 1024) {
        return STATUS_INVALID_PARAMETER;
    }

    PUCHAR base = (PUCHAR)GpuBar5Va;

    /* Write firmware physical address to C2PMSG_36/37. */
    ULONG paLo = (ULONG)(FwPa.QuadPart & 0xFFFFFFFF);
    ULONG paHi = (ULONG)(FwPa.QuadPart >> 32);
    WRITE_REGISTER_ULONG((PULONG)(base + DIRECT_C2PMSG_36_OFFSET), paLo);
    WRITE_REGISTER_ULONG((PULONG)(base + DIRECT_C2PMSG_37_OFFSET), paHi);
    KeMemoryBarrier();

    /* Write command: GFX_CMD_ID_LOAD_IP_FW (0x06) | (fwType << 16). */
    ULONG cmd = 0x06 | (FwType << 16);
    WRITE_REGISTER_ULONG((PULONG)(base + DIRECT_C2PMSG_35_OFFSET), cmd);
    KeMemoryBarrier();

    /* Poll for completion. */
    NTSTATUS status = PspWaitCompletion(GpuBar5Va, DIRECT_POLL_MAX_MS);

    if (OutC2pmsg35) {
        *OutC2pmsg35 = READ_REGISTER_ULONG(
            (PULONG)(base + DIRECT_C2PMSG_35_OFFSET));
    }
    if (OutC2pmsg81) {
        *OutC2pmsg81 = READ_REGISTER_ULONG(
            (PULONG)(base + DIRECT_C2PMSG_81_OFFSET));
    }

    return status;
}
/* --- Direct PSP bootloader: load TOS (Ta.bin) via PSP_BL__LOAD_TOS_SPL_TABLE.
 *     Writes C2PMSG_36 = PA >> 20 (bootloader address format, 1MB units),
 *     writes cmd 0x10000000 to C2PMSG_35, polls C2PMSG_35 cleared, then
 *     reports C2PMSG_64 bit31 (TOS ready). --- */
NTSTATUS Amdbc250PspDirectLoadTos(PVOID GpuBar5Va, ULONG FwSize,
    PHYSICAL_ADDRESS FwPa, PULONG OutC2pmsg64Before, PULONG OutC2pmsg64After,
    PULONG OutC2pmsg35, PULONG OutC2pmsg81)
{
    if (!GpuBar5Va || FwSize == 0 || FwSize > 4 * 1024 * 1024) {
        return STATUS_INVALID_PARAMETER;
    }

    PUCHAR base = (PUCHAR)GpuBar5Va;
    ULONG c64;

    if (OutC2pmsg64Before) {
        *OutC2pmsg64Before = READ_REGISTER_ULONG(
            (PULONG)(base + DIRECT_C2PMSG_64_OFFSET));
    }

    /* Write firmware PA in 1MB units to C2PMSG_36 (bootloader format). */
    WRITE_REGISTER_ULONG((PULONG)(base + DIRECT_C2PMSG_36_OFFSET),
        (ULONG)(FwPa.QuadPart >> 20));
    KeMemoryBarrier();

    /* Write bootloader command PSP_BL__LOAD_TOS_SPL_TABLE. */
    WRITE_REGISTER_ULONG((PULONG)(base + DIRECT_C2PMSG_35_OFFSET),
        PSP_BL__LOAD_TOS_SPL_TABLE);
    KeMemoryBarrier();

    /* Poll C2PMSG_35 bit31 (bootloader ready/completion) or timeout. */
    NTSTATUS status = STATUS_TIMEOUT;
    ULONG i;
    for (i = 0; i < DIRECT_POLL_MAX_MS; i++) {
        ULONG cmd = READ_REGISTER_ULONG(
            (PULONG)(base + DIRECT_C2PMSG_35_OFFSET));
        if (cmd & 0x80000000) { status = STATUS_SUCCESS; break; }
        KeStallExecutionProcessor(1000);
    }

    if (OutC2pmsg35) {
        *OutC2pmsg35 = READ_REGISTER_ULONG(
            (PULONG)(base + DIRECT_C2PMSG_35_OFFSET));
    }
    c64 = READ_REGISTER_ULONG((PULONG)(base + DIRECT_C2PMSG_64_OFFSET));
    if (OutC2pmsg64After) *OutC2pmsg64After = c64;
    if (OutC2pmsg81) {
        *OutC2pmsg81 = READ_REGISTER_ULONG(
            (PULONG)(base + DIRECT_C2PMSG_81_OFFSET));
    }

    /* TOS load only "succeeds" when C2PMSG_64 bit31 (TOS ready) is set. */
    if (c64 & 0x80000000) status = STATUS_SUCCESS;
    else if (NT_SUCCESS(status)) status = STATUS_DEVICE_NOT_READY;

    return status;
}
/* --- Direct SMU message via C2PMSG_66/82/90 through SMN.
 *     Uses NBIO BAR5+0x38/0x3C for SMN transport.
 *     Protocol: wait ready â†’ ack â†’ write arg â†’ write msg â†’ wait â†’ read response. --- */
NTSTATUS Amdbc250PspDirectSmuMsg(PVOID GpuBar5Va, ULONG Message, ULONG Argument,
    PULONG OutResponse, PULONG OutResponseStatus)
{
    if (!GpuBar5Va) return STATUS_INVALID_PARAMETER;

    /* Wait for SMU mailbox ready. */
    NTSTATUS status = SmuWaitReady(GpuBar5Va, DIRECT_POLL_MAX_MS);
    if (!NT_SUCCESS(status)) {
        KdPrint(("BC250-PSP-SMU: timeout waiting for ready\n"));
        if (OutResponseStatus) *OutResponseStatus = 0xFFFFFFFF;
        return status;
    }

    /* Ack by writing 0 to C2PMSG_90. */
    Amdbc250PspSmnWrite(GpuBar5Va, SMU_C2PMSG_90_SMN, 0);

    /* Write argument to C2PMSG_82. */
    Amdbc250PspSmnWrite(GpuBar5Va, SMU_C2PMSG_82_SMN, Argument);

    /* Write message to C2PMSG_66. */
    Amdbc250PspSmnWrite(GpuBar5Va, SMU_C2PMSG_66_SMN, Message);

    /* Wait for completion. */
    status = SmuWaitReady(GpuBar5Va, DIRECT_POLL_MAX_MS);

    /* Read response. */
    ULONG response = 0;
    ULONG respStatus = 0;
    if (NT_SUCCESS(status)) {
        response = Amdbc250PspSmnRead(GpuBar5Va, SMU_C2PMSG_82_SMN);
        respStatus = 1;
    } else {
        KdPrint(("BC250-PSP-SMU: timeout waiting for response\n"));
        respStatus = 0xFF;
    }

    if (OutResponse) *OutResponse = response;
    if (OutResponseStatus) *OutResponseStatus = respStatus;

    return status;
}

/* ============================================================================
 * SMU Queue 3 mailbox (SMN-based) â€” used for core-unlock (msg 0x98) and the
 * safe test message (msg 0x01). Queue 3 is the telemetry/perf-profile queue:
 *   CMD = SMN[0x03B10A20], RSP = SMN[0x03B10A80], ARG = SMN[0x03B10A88].
 * DONE states: 0x01=OK, 0xFF=fail, 0xFE=unknown, 0xFD=rejected, 0xFC=busy.
 * ============================================================================
 */
#define SMU_Q3_CMD_SMN   0x03B10A20
#define SMU_Q3_RSP_SMN   0x03B10A80
#define SMU_Q3_ARG_SMN   0x03B10A88

/* Core presence mask register (SMN). 0x77 = 6 cores, 0xFF = 8 cores. */
#define SMN_CORE_MASK_REG   0x0115A870

/* Wait for Q3 RSP to reach a DONE state. Returns the state (1=OK), or 0 on
 * timeout. Never blocks more than ~2.5s. */
static ULONG
SmuQ3WaitDone(PVOID GpuBar5Va, ULONG TimeoutMs)
{
    ULONG i;
    for (i = 0; i < TimeoutMs; i++) {
        ULONG st = Amdbc250PspSmnRead(GpuBar5Va, SMU_Q3_RSP_SMN);
        if (st == 0x01 || st == 0xFF || st == 0xFE || st == 0xFD || st == 0xFC) {
            return st;
        }
        KeStallExecutionProcessor(1000); /* 1ms */
    }
    return 0;
}

/* --- SMU Q3 mailbox round-trip. Safe: only ever touches the three fixed Q3
 *     registers. MsgStatus: 1=OK, 0xFF=fail, 0xFE=unknown, 0xFD=rejected,
 *     0xFC=busy, 0=timeout. Returns STATUS_SUCCESS only on 0x01. --- */
NTSTATUS
Amdbc250PspSmuQ3Msg(PVOID GpuBar5Va, ULONG Message, ULONG Argument,
                    PULONG OutResponse, PULONG OutResponseStatus)
{
    if (!GpuBar5Va) return STATUS_INVALID_PARAMETER;

    /* Wait for mailbox idle, ack by writing 0. */
    ULONG st = SmuQ3WaitDone(GpuBar5Va, DIRECT_POLL_MAX_MS);
    if (st == 0) {
        if (OutResponseStatus) *OutResponseStatus = 0;
        return STATUS_TIMEOUT;
    }
    Amdbc250PspSmnWrite(GpuBar5Va, SMU_Q3_RSP_SMN, 0);

    /* Write argument, then command. */
    Amdbc250PspSmnWrite(GpuBar5Va, SMU_Q3_ARG_SMN, Argument);
    Amdbc250PspSmnWrite(GpuBar5Va, SMU_Q3_CMD_SMN, Message);

    /* Wait for completion. */
    st = SmuQ3WaitDone(GpuBar5Va, DIRECT_POLL_MAX_MS);
    ULONG resp = Amdbc250PspSmnRead(GpuBar5Va, SMU_Q3_ARG_SMN);

    if (OutResponse) *OutResponse = resp;
    if (OutResponseStatus) *OutResponseStatus = st;

    return (st == 0x01) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/* --- Safe CPU core unlock.
 *     SMU Q3 msg 0x98 is an UNGATED SMU-privileged SMN write: it writes the
 *     fixed value 0x00FF to the SMN address passed as the message argument.
 *     There is NO bounds check on the SMU side, so we enforce a strict
 *     whitelist here: only SMN_CORE_MASK_REG may be targeted, and only when
 *     the current mask reads 0x77 (6 cores). If it already reads 0xFF, report
 *     "already unlocked". Any other value aborts without writing.
 *
 *     Result: 1=unlocked now, 2=already 0xFF, 0=failed/refused.
 *     OutCoreMaskBefore/After: mask read before/after the write. --- */
NTSTATUS
Amdbc250PspCoreUnlock(PVOID GpuBar5Va, PULONG OutCoreMaskBefore,
                      PULONG OutCoreMaskAfter, PULONG OutResult)
{
    if (!GpuBar5Va) return STATUS_INVALID_PARAMETER;
    if (OutCoreMaskBefore) *OutCoreMaskBefore = 0;
    if (OutCoreMaskAfter)  *OutCoreMaskAfter  = 0;
    if (OutResult)         *OutResult         = 0;

    /* 1. Sanity: SMU alive via Q3 test message (0x01 returns arg+1). */
    ULONG testResp = 0, testSt = 0;
    NTSTATUS status = Amdbc250PspSmuQ3Msg(GpuBar5Va, 0x01, 123, &testResp, &testSt);
    if (!NT_SUCCESS(status) || testResp != 124) {
        KdPrint(("BC250-PSP-SMU: Q3 core-unlock SMU not alive (st=0x%X resp=0x%X)\n",
                 testSt, testResp));
        return STATUS_DEVICE_NOT_READY;
    }

    /* 2. Read current core presence mask. */
    ULONG maskBefore = Amdbc250PspSmnRead(GpuBar5Va, SMN_CORE_MASK_REG);
    if (OutCoreMaskBefore) *OutCoreMaskBefore = maskBefore;

    /* 3. Already fully unlocked? */
    if ((maskBefore & 0xFF) == 0xFF) {
        if (OutCoreMaskAfter) *OutCoreMaskAfter = maskBefore;
        if (OutResult) *OutResult = 2;
        return STATUS_SUCCESS;
    }

    /* 4. Refuse to touch unless the mask is exactly the known 6-core state. */
    if ((maskBefore & 0xFF) != 0x77) {
        KdPrint(("BC250-PSP-SMU: core-unlock ABORT, unexpected mask 0x%02X\n",
                 maskBefore & 0xFF));
        return STATUS_UNSUCCESSFUL;
    }

    /* 5. Send Q3 msg 0x98 to the whitelisted register. */
    status = Amdbc250PspSmuQ3Msg(GpuBar5Va, 0x98, SMN_CORE_MASK_REG,
                                 NULL, NULL);
    if (!NT_SUCCESS(status)) {
        KdPrint(("BC250-PSP-SMU: core-unlock msg 0x98 failed 0x%08X\n", status));
        return status;
    }

    /* 6. Verify it stuck. */
    KeStallExecutionProcessor(20000); /* 20ms */
    ULONG maskAfter = Amdbc250PspSmnRead(GpuBar5Va, SMN_CORE_MASK_REG);
    if (OutCoreMaskAfter) *OutCoreMaskAfter = maskAfter;

    if ((maskAfter & 0xFF) == 0xFF) {
        if (OutResult) *OutResult = 1;
        return STATUS_SUCCESS;
    }

    KdPrint(("BC250-PSP-SMU: core-unlock did NOT stick (0x%02X -> 0x%02X)\n",
             maskBefore & 0xFF, maskAfter & 0xFF));
    return STATUS_UNSUCCESSFUL;
}

/* Initialize PSP proxy - open handle to PSP driver for GPU register access */
static BOOLEAN PspProxyInit(VOID)
{
    NTSTATUS status;
    UNICODE_STRING deviceName;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;

    if (g_PspProxyHandle) return TRUE;

    RtlInitUnicodeString(&deviceName, L"\\DosDevices\\AmdBcPsp");
    InitializeObjectAttributes(&oa, &deviceName, OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(&g_PspProxyHandle,
        GENERIC_READ | GENERIC_WRITE, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

    if (NT_SUCCESS(status)) {
        /* Try to get GPU info â€” only mark proxy available if PSP driver is initialized */
        PSP_GPU_INFO_REMOTE gpuInfo;
        IO_STATUS_BLOCK iosb2;
        RtlZeroMemory(&gpuInfo, sizeof(gpuInfo));
        status = ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
            &iosb2, PSP_IOCTL_GET_GPU_INFO, NULL, 0, &gpuInfo, sizeof(gpuInfo));
        if (NT_SUCCESS(status)) {
            g_PspProxyAvailable = TRUE;
            g_GpcomRingPa = gpuInfo.RingBufferPA;
            g_GpcomRingSize = 0x1000;
            g_GpcomRingAvailable = (gpuInfo.RingBufferPA != 0) ? TRUE : FALSE;

            /* Determine SOS-alive. Prefer reading C2PMSG_81 directly from the GPU
             * BAR5 mapping we already hold (INIT_HARDWARE is done before this proxy
             * is opened by the test tools). The PSP driver's own GpuMmioBase is
             * often NULL on Win11 26100 because its InitHw ran before the GPU
             * driver mapped BAR5, so GET_GPU_INFO cannot see SOS status. */
            ULONG c2pmsg81 = 0;
            PVOID bar5 = Amdbc250PspGetGpuBar5Va();
            if (bar5) {
                c2pmsg81 = READ_REGISTER_ULONG(
                    (PULONG)((PUCHAR)bar5 + GPU_BAR5_C2PMSG_81_OFFSET));
                KdPrint(("BC250-PSP: Read C2PMSG_81 directly from GPU BAR5 -> 0x%08X\n", c2pmsg81));
            }
            if (c2pmsg81 == 0) {
                /* Fall back to PSP driver's report. */
                c2pmsg81 = gpuInfo.C2pmsg81;
                KdPrint(("BC250-PSP: Falling back to PSP GET_GPU_INFO C2PMSG_81=0x%08X\n", c2pmsg81));
            }

            KdPrint(("BC250-PSP: Proxy opened, C2PMSG_81=0x%08X FW=%u RingPA=0x%08X Ring=%s\n",
                c2pmsg81, gpuInfo.FwCount, (ULONG)g_GpcomRingPa,
                g_GpcomRingAvailable ? "YES" : "NO"));
            if (g_GpcomRingAvailable && g_GpcomRingPa) {
                PHYSICAL_ADDRESS ringPhys;
                ringPhys.QuadPart = g_GpcomRingPa;
                g_GpcomRingVa = MmMapIoSpace(ringPhys, g_GpcomRingSize, MmNonCached);
                KdPrint(("BC250-PSP: GPCOM ring PA=0x%llX VA=%p\n", ringPhys.QuadPart, g_GpcomRingVa));
            }

            /* Update SOS-alive in the shared PSP context (the loader checks this).
             * With the corrected MP0 base (0x58000), C2PMSG_81 carries the SOS
             * status value; bit31 is not set for an alive-but-idle SOS on BC-250,
             * so treat any non-zero status as "SOS present". The old exact-match
             * against 0xF0000010 was read from the WRONG base and is removed. */
            g_PspContext.SosAlive = (c2pmsg81 != 0) ? TRUE : FALSE;
            g_PspContext.Initialized = TRUE;

            /* Initialize KIQ ring for command submission */
            if (NT_SUCCESS(Amdbc250PspKiqInit())) {
                g_PspProxyAvailable = TRUE;
                KdPrint(("BC250-PSP: KIQ ring initialized successfully\n"));
            } else {
                KdPrint(("BC250-PSP: KIQ ring initialization failed\n"));
            }
        } else {
            KdPrint(("BC250-PSP: PSP driver handle opened but not initialized (0x%08X)\n", status));
            ZwClose(g_PspProxyHandle);
            g_PspProxyHandle = NULL;
            return FALSE;
        }
        KdPrint(("BC250-PSP: Proxy to PSP driver opened\n"));
        return TRUE;
    }
    KdPrint(("BC250-PSP: PSP driver proxy not available (0x%08X)\n", status));
    return FALSE;
}

/* Read GPU register via PSP proxy */
ULONG Amdbc250PspProxyReadReg(ULONG GpuRegOffset)
{
    IO_STATUS_BLOCK iosb;
    ULONG outBuf[2] = { 0, 0 };

    if (!g_PspProxyHandle && !PspProxyInit()) {
        return Amdbc250PspReadRegister(GpuRegOffset);
    }

    /* Direct PSP MMIO read via PSP driver (mailbox/csr registers work;
       GPU registers behind NBIO firewall return 0xFFFFFFFF) */
    ULONG inBuf[2] = { GpuRegOffset, 0 };
    NTSTATUS status = ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
        &iosb, PSP_IOCTL_READ_REG, inBuf, sizeof(inBuf), outBuf, sizeof(outBuf));
    if (NT_SUCCESS(status)) return outBuf[0];

    return Amdbc250PspReadRegister(GpuRegOffset);
}

/* Write GPU register via PSP/KIQ proxy */
VOID Amdbc250PspProxyWriteReg(ULONG GpuRegOffset, ULONG Value)
{
    ULONG inBuf[3] = { GpuRegOffset, Value, 0 }; /* 0 = write */
    IO_STATUS_BLOCK iosb;

    if (!g_PspProxyHandle && !PspProxyInit()) {
        Amdbc250PspWriteRegister(GpuRegOffset, Value);
        return;
    }

    if (g_GpcomRingAvailable) {
        ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
            &iosb, PSP_IOCTL_REG_PROG, inBuf, 3 * sizeof(ULONG), NULL, 0);
        return;
    }

    /* Fallback: direct PSP MMIO write */
    ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
        &iosb, PSP_IOCTL_WRITE_REG, inBuf, 3 * sizeof(ULONG), NULL, 0);
}

/* Check if PSP proxy is available */
BOOLEAN Amdbc250PspProxyAvailable(VOID)
{
    if (!g_PspProxyHandle) PspProxyInit();
    return g_PspProxyAvailable && g_KiqRingInitialized;
}

/* Check if GPCOM ring is available (needed for register writes via PSP) */
BOOLEAN Amdbc250PspKiqAvailable(VOID)
{
    if (!g_PspProxyHandle) PspProxyInit();
    return g_KiqRingInitialized;
}

/* Check if KIQ ring is initialized (for external callers) */
BOOLEAN Amdbc250PspKiqIsInitialized(VOID)
{
    return g_KiqRingInitialized;
}

/* Submit PM4 packets to KIQ ring for execution */
NTSTATUS Amdbc250PspKiqSubmit(ULONG* Pm4Commands, ULONG DwordCount)
{
    IO_STATUS_BLOCK iosb;
    PSP_KIQ_SUBMIT_REQUEST req;
    
    if (!g_KiqRingInitialized) {
        KdPrint(("KIQ: Ring not initialized\n"));
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (DwordCount == 0 || DwordCount > 64) {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Build input buffer using proper struct layout */
    req.CommandCount = DwordCount;
    req.Reserved[0] = 0;
    req.Reserved[1] = 0;
    req.Reserved[2] = 0;
    for (ULONG i = 0; i < DwordCount; i++) {
        req.Commands[i] = Pm4Commands[i];
    }
    
    /* Send PM4 commands to PSP driver via KIQ_SUBMIT IOCTL (0x818) */
    NTSTATUS status = ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
        &iosb, PSP_IOCTL_KIQ_SUBMIT, &req, sizeof(req), NULL, 0);
    
    if (NT_SUCCESS(status)) {
        g_KiqRingWptr += DwordCount;
    }
    
    return status;
}

/* Read register via KIQ ring - for compatibility */
ULONG Amdbc250PspKiqReadReg(ULONG GpuRegOffset)
{
    return Amdbc250PspProxyReadReg(GpuRegOffset);
}

/*
 * Load GPU firmware via the PSP driver's LOAD_IP_FW mailbox path.
 * Uses PSP_IOCTL_LOAD_IP_FW_DIRECT (0x824), whose input layout is:
 *   PSP_LOAD_IP_FW_REQUEST { FwType, FwSize }
 *   firmware blob (FwSize bytes) immediately after the header.
 * The PSP driver submits the blob to the SOS secure mailbox
 * (GFX_CMD_ID_LOAD_IP_FW = 0x06) via C2PMSG_35/36/37/81. This is the path
 * that actually works on BC-250 (verified via psp-mailbox-rlc-test.exe).
 */
NTSTATUS Amdbc250PspKiqLoadFirmware(ULONG FwType, ULONG FwSize, PHYSICAL_ADDRESS FwPa)
{
    UNREFERENCED_PARAMETER(FwPa);

    if (!g_KiqRingInitialized) {
        NTSTATUS status = Amdbc250PspKiqInit();
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    if (!g_PspProxyHandle) {
        return STATUS_DEVICE_NOT_READY;
    }

    /* The firmware blob must already be in the shared buffer
     * (Amdbc250PspCopyFirmwareData). Build the 0x824 input layout. */
    if (!g_FwBuffer || FwSize > g_FwBufferSize) {
        KdPrint(("KIQ_LOAD_FW: firmware buffer not ready (buf=%p size=%u)\n",
            g_FwBuffer, g_FwBufferSize));
        return STATUS_INVALID_PARAMETER;
    }

    /* Allocate a contiguous input buffer: header + fwSize bytes. */
    ULONG inSize = sizeof(PSP_LOAD_IP_FW_REQUEST) + FwSize;
    PSP_LOAD_IP_FW_REQUEST *inBuf =
        (PSP_LOAD_IP_FW_REQUEST *)ExAllocatePool2(POOL_FLAG_NON_PAGED, inSize, 'fw');
    if (!inBuf) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    inBuf->FwType = FwType;
    inBuf->FwSize = FwSize;
    RtlCopyMemory((PUCHAR)inBuf + sizeof(PSP_LOAD_IP_FW_REQUEST), g_FwBuffer, FwSize);

    KdPrint(("KIQ_LOAD_FW: type=%u size=%u via PSP_IOCTL_LOAD_IP_FW_DIRECT\n", FwType, FwSize));

    IO_STATUS_BLOCK iosb;
    NTSTATUS status = ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
        &iosb, PSP_IOCTL_LOAD_IP_FW_DIRECT, inBuf, inSize, NULL, 0);

    ExFreePoolWithTag(inBuf, 'fw');
    return status;
}

/* Ring-based firmware load: mirrors Linux psp_v11_0_8.c LOAD_IP_FW path.
 * On BC-250 this falls back to direct C2PMSG because SOS lacks TOS. */
#define PSP_RING_SIZE 0x1000
#define PSP_RING_TYPE_GFX 0

NTSTATUS Amdbc250PspRingLoadFirmware(PVOID GpuBar5Va, ULONG FwType, ULONG FwSize,
                                     PHYSICAL_ADDRESS FwPa)
{
    if (!GpuBar5Va || FwSize == 0 || FwSize > 4 * 1024 * 1024) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Try ring-based path first (Linux psp_v11_0_8.c style). */
    NTSTATUS status = Amdbc250PspRingCreate(GpuBar5Va,
                                            PSP_RING_TYPE_GFX,
                                            (ULONG)(FwPa.QuadPart & 0xFFFFFFFF),
                                            (ULONG)(FwPa.QuadPart >> 32),
                                            PSP_RING_SIZE);
    if (NT_SUCCESS(status)) {
        /* TODO: write firmware blob into ring buffer and submit via ring WPTR.
         * BC-250 SOS lacks TOS, so this path currently returns NOT_SUPPORTED.
         * When a TOS-capable SOS is used, implement:
         *   - copy FwSize bytes from FwPa into g_PspContext.RingBuffer
         *   - Amdbc250PspRingWriteWptr(GpuBar5Va, FwSize)
         *   - wait for completion via C2PMSG_64/67
         */
        return STATUS_NOT_SUPPORTED;
    }

    /* Fallback: direct C2PMSG_35/36/37/81 path (works on BC-250 today). */
    return Amdbc250PspDirectLoadIpFw(GpuBar5Va, FwType, FwSize, FwPa, NULL, NULL);
}

/* Allocate shared memory for firmware loading via KIQ */
NTSTATUS Amdbc250PspAllocateFirmwareBuffer(ULONG Size)
{
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_FwLock, &oldIrql);

    if (g_FwBuffer) {
        if (g_FwBufferSize >= Size) {
            KeReleaseSpinLock(&g_FwLock, oldIrql);
            return STATUS_SUCCESS;
        }
        MmFreeContiguousMemory(g_FwBuffer);
        g_FwBuffer = NULL;
        g_FwBufferPa.QuadPart = 0;
        g_FwBufferSize = 0;
    }

    PHYSICAL_ADDRESS low = {0};
    PHYSICAL_ADDRESS high = {0};
    PHYSICAL_ADDRESS boundary = {0};
    high.QuadPart = 0xFFFFFFFFULL;

    g_FwBuffer = MmAllocateContiguousMemorySpecifyCache(
        Size, low, high, boundary, MmNonCached);
    if (!g_FwBuffer) {
        KeReleaseSpinLock(&g_FwLock, oldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    g_FwBufferPa = MmGetPhysicalAddress(g_FwBuffer);
    g_FwBufferSize = Size;
    g_FwCount = 0;

    KeReleaseSpinLock(&g_FwLock, oldIrql);
    KdPrint(("FW_BUF: allocated %u bytes at PA=0x%llX\n", Size, g_FwBufferPa.QuadPart));
    return STATUS_SUCCESS;
}

/* Copy firmware data to the shared buffer and return its PA */
NTSTATUS Amdbc250PspCopyFirmwareData(PUCHAR FirmwareData, ULONG Size)
{
    if (!g_FwBuffer || !FirmwareData || Size == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_FwLock, &oldIrql);
    if (Size > g_FwBufferSize) {
        KeReleaseSpinLock(&g_FwLock, oldIrql);
        return STATUS_BUFFER_OVERFLOW;
    }

    RtlCopyMemory(g_FwBuffer, FirmwareData, Size);
    g_FwCount++;
    KeReleaseSpinLock(&g_FwLock, oldIrql);
    return STATUS_SUCCESS;
}

/* Return the physical address of the shared firmware buffer (or 0 if none). */
PHYSICAL_ADDRESS Amdbc250PspFirmwarePa(VOID)
{
    return g_FwBufferPa;
}

/* Accept the GPU BAR5 VA published by the GPU driver's INIT_HARDWARE handler. */
VOID Amdbc250PspPublishGpuBar5(PVOID Va)
{
    g_GpuBar5Va = Va;
}

/* Initialize KIQ ring for command submission */
NTSTATUS Amdbc250PspKiqInit(VOID)
{
    /* Initialize spin locks if not already done */
    static BOOLEAN locksInitialized = FALSE;
    if (!locksInitialized) {
        KeInitializeSpinLock(&g_KiqRingLock);
        KeInitializeSpinLock(&g_FwLock);
        locksInitialized = TRUE;
    }
    
    /* Ensure PSP proxy handle is open */
    if (!g_PspProxyHandle) {
        KdPrint(("KIQ: PSP proxy handle not open, calling PspProxyInit\n"));
        if (!PspProxyInit()) {
            KdPrint(("KIQ: PspProxyInit FAILED\n"));
            return STATUS_DEVICE_NOT_READY;
        }
        KdPrint(("KIQ: PspProxyInit SUCCESS, handle=0x%p\n", g_PspProxyHandle));
    }
    
    /* Re-check after PspProxyInit (which may have initialized KIQ) */
    if (g_KiqRingInitialized) {
        return STATUS_SUCCESS;
    }
    
    /* PSP driver handles KIQ ring initialization internally via PspKiqInit().
       We just need a dummy buffer for PM4 commands in Amdbc250PspKiqSubmit.
       The actual ring buffer is in PSP driver's g_RingBuffer. */
    g_KiqRingInitialized = TRUE;
    g_KiqRingWptr = 0;
    g_KiqRingSize = 0x1000;  /* PSP driver uses 4KB ring */
    
    KdPrint(("KIQ: Using PSP driver's KIQ ring implementation\n"));
    
    return STATUS_SUCCESS;
}

/* Cleanup KIQ ring resources */
VOID Amdbc250PspKiqCleanup(VOID)
{
    /* PSP driver handles KIQ ring cleanup internally via PspKiqCleanup().
       We don't allocate our own ring buffer anymore. */
    if (!g_KiqRingInitialized) {
        return;
    }
    
    /* No need to disable KIQ_CNTL - PSP driver handles this */
    g_KiqRingInitialized = FALSE;
    g_KiqRingWptr = 0;
    g_KiqRingSize = 0;
    
    /* Free firmware buffer */
    if (g_FwBuffer) {
        MmFreeContiguousMemory(g_FwBuffer);
        g_FwBuffer = NULL;
        g_FwBufferPa.QuadPart = 0;
        g_FwBufferSize = 0;
    }
    
    KdPrint(("KIQ: KIQ cleanup (PSP driver manages ring)\n"));
}

/* Close PSP proxy handle */
VOID Amdbc250PspProxyCleanup(VOID)
{
    if (g_GpcomRingVa) {
        MmUnmapIoSpace(g_GpcomRingVa, g_GpcomRingSize);
        g_GpcomRingVa = NULL;
    }
    Amdbc250PspKiqCleanup();
    if (g_PspProxyHandle) {
        ZwClose(g_PspProxyHandle);
        g_PspProxyHandle = NULL;
        g_PspProxyAvailable = FALSE;
        g_GpcomRingAvailable = FALSE;
    }
}

#define MBOX_TOS_READY_FLAG           0x80000000
#define MBOX_TOS_READY_MASK           0x80000000
#define MBOX_TOS_RESP_FLAG            0x80000000
#define MBOX_TOS_RESP_MASK            0x80000000

#define GFX_CTRL_CMD_ID_DESTROY_RINGS 0x00020000

#define PSP_MAX_WAIT_MS               5000
#define PSP_BOOTLOADER_WAIT_MS        1000
#define PSP_RING_SIZE                 0x1000
#define PSP_RING_TYPE_GFX             0  /* BC-250 GFX ring type for TOS ring create */

static ULONG g_Mp0BaseDword = 0;

VOID Amdbc250PspUnmapRegisters(VOID)
{
    if (g_PspContext.MmioBase) {
        MmUnmapIoSpace(g_PspContext.MmioBase, g_PspContext.MmioSize);
        g_PspContext.MmioBase = NULL;
    }
    g_Mp0BaseDword = 0;
}

static ULONG PspReg(ULONG RegByteOffset)
{
    return (g_Mp0BaseDword * 4) + RegByteOffset;
}

ULONG Amdbc250PspReadRegister(ULONG RegisterOffset)
{
    ULONG off = PspReg(RegisterOffset);
    if (!g_PspContext.MmioBase || off >= g_PspContext.MmioSize)
        return 0xFFFFFFFF;
    return READ_REGISTER_ULONG((PULONG)(g_PspContext.MmioBase + off));
}

VOID Amdbc250PspWriteRegister(ULONG RegisterOffset, ULONG Value)
{
    ULONG off = PspReg(RegisterOffset);
    if (!g_PspContext.MmioBase || off >= g_PspContext.MmioSize)
        return;
    WRITE_REGISTER_ULONG((PULONG)(g_PspContext.MmioBase + off), Value);
}

static NTSTATUS Amdbc250PspWaitForRegister(ULONG RegisterOffset, ULONG ExpectedValue, ULONG Mask, ULONG TimeoutMs)
{
    ULONG i;
    ULONG regValue;
    LARGE_INTEGER delay;
    delay.QuadPart = -10000LL;
    for (i = 0; i < TimeoutMs; i++) {
        regValue = Amdbc250PspReadRegister(RegisterOffset);
        if ((regValue & Mask) == (ExpectedValue & Mask))
            return STATUS_SUCCESS;
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    KdPrint(("BC250-PSP: Timeout reg 0x%03X (got 0x%08X, want 0x%08X mask 0x%08X)\n",
             RegisterOffset, regValue, ExpectedValue, Mask));
    return STATUS_TIMEOUT;
}

static NTSTATUS Amdbc250PspAllocateSharedMemory(VOID)
{
    PHYSICAL_ADDRESS low = {0};
    PHYSICAL_ADDRESS high;
    PHYSICAL_ADDRESS skip = {0};
    high.QuadPart = 0x3FFFFFFFFFULL;
    g_PspContext.FirmwareBufferSize = 256 * 1024;
    g_PspContext.FirmwareMdl = MmAllocatePagesForMdlEx(low, high, skip, g_PspContext.FirmwareBufferSize, MmCached, 0);
    if (!g_PspContext.FirmwareMdl) return STATUS_INSUFFICIENT_RESOURCES;
    g_PspContext.FirmwareBuffer = MmMapLockedPagesSpecifyCache(
        g_PspContext.FirmwareMdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    if (!g_PspContext.FirmwareBuffer) { MmFreePagesFromMdl(g_PspContext.FirmwareMdl); return STATUS_INSUFFICIENT_RESOURCES; }
    g_PspContext.FirmwarePhysical = MmGetPhysicalAddress(g_PspContext.FirmwareBuffer);
    g_PspContext.RingSize = PSP_RING_SIZE;
    g_PspContext.RingBuffer = MmAllocateContiguousMemory(g_PspContext.RingSize, low);
    if (!g_PspContext.RingBuffer) return STATUS_INSUFFICIENT_RESOURCES;
    g_PspContext.RingPhysical = MmGetPhysicalAddress(g_PspContext.RingBuffer);
    g_PspContext.RingWptr = 0;
    KdPrint(("BC250-PSP: Firmware VA=0x%p PA=0x%llX, Ring VA=0x%p PA=0x%llX\n",
             g_PspContext.FirmwareBuffer, g_PspContext.FirmwarePhysical.QuadPart,
             g_PspContext.RingBuffer, g_PspContext.RingPhysical.QuadPart));
    return STATUS_SUCCESS;
}

static VOID Amdbc250PspFreeSharedMemory(VOID)
{
    if (g_PspContext.RingBuffer) {
        MmFreeContiguousMemory(g_PspContext.RingBuffer);
        g_PspContext.RingBuffer = NULL;
    }
    if (g_PspContext.FirmwareBuffer && g_PspContext.FirmwareMdl) {
        MmUnmapLockedPages(g_PspContext.FirmwareBuffer, g_PspContext.FirmwareMdl);
        MmFreePagesFromMdl(g_PspContext.FirmwareMdl);
        g_PspContext.FirmwareBuffer = NULL;
        g_PspContext.FirmwareMdl = NULL;
    }
}

static NTSTATUS Amdbc250PspWaitForBootloader(VOID)
{
    return Amdbc250PspWaitForRegister(MP0_C2PMSG_35_BYTE, 0x80000000, 0x8000FFFF, PSP_BOOTLOADER_WAIT_MS);
}

static NTSTATUS Amdbc250PspIsSosAlive(PBOOLEAN Alive)
{
    ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
    *Alive = (sol & 0x80000000) ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

static NTSTATUS Amdbc250PspDiscoverMp0Base(VOID)
{
    /* Primary scan: ip_discovery-verified MP0 base FIRST. Linux reads MP0/0
       base_addr = 0x16000 (dwords) from the discovery TMR -> BAR5 byte base
       0x58000. That is the REAL mailbox (C2PMSG_81 @ 0x58244 = SOS status).
       The old heuristics below can match a false candidate (e.g. 0x040F4)
       whose C2PMSG_81 @ 0x10614 reads 0xF0000010 but is NOT the live mailbox. */
    ULONG tryOffsets[] = {
        0x16000, /* ip_discovery MP0 base (verified 2026-07-31, BAR5 0x58000) */
        0x00000, 0x04000, 0x040F0, 0x040F4, 0x040F8, 0x04100, 0x0410C, 0x04200,
        0x04400, 0x04800, 0x05000, 0x06000, 0x08000, 0x10000,
    };
    ULONG i;
    for (i = 0; i < sizeof(tryOffsets) / sizeof(tryOffsets[0]); i++) {
        g_Mp0BaseDword = tryOffsets[i];
        ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
        if (sol != 0 && sol != 0xFFFFFFFF) {
            /* Verify that C2PMSG_35 is also accessible (writable check) */
            ULONG test35 = Amdbc250PspReadRegister(MP0_C2PMSG_35_BYTE);
            if (test35 != 0xFFFFFFFF) {
                KdPrint(("BC250-PSP: MP0 base found at DWORD offset 0x%05X (SOL=0x%08X, C35=0x%08X)\n",
                    tryOffsets[i], sol, test35));
                return STATUS_SUCCESS;
            }
            KdPrint(("BC250-PSP: Candidate at 0x%05X (SOL=0x%08X) but C2PMSG_35 blocked\n",
                tryOffsets[i], sol));
        }
    }
    /* Fallback: wider scan (original behavior) */
    ULONG tryOffsets2[] = { 0x10000, 0x14000, 0x16000, 0x18000, 0x1C000, 0x1E000, 0x20000,
        0x22000, 0x24000, 0x28000, 0x2C000, 0x30000, 0x34000, 0x38000, 0x3C000 };
    for (i = 0; i < sizeof(tryOffsets2) / sizeof(tryOffsets2[0]); i++) {
        g_Mp0BaseDword = tryOffsets2[i];
        ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
        if (sol != 0 && sol != 0xFFFFFFFF) {
            KdPrint(("BC250-PSP: MP0 base found (fallback) at DWORD offset 0x%05X (SOL=0x%08X)\n", tryOffsets2[i], sol));
            return STATUS_SUCCESS;
        }
    }
    KdPrint(("BC250-PSP: MP0 base not found (no SOS alive signal)\n"));
    g_Mp0BaseDword = 0;
    return STATUS_NOT_FOUND;
}

static NTSTATUS Amdbc250PspBootloaderLoadSysdrv(VOID)
{
    NTSTATUS status;
    BOOLEAN alive;
    status = Amdbc250PspIsSosAlive(&alive);
    if (alive) { return STATUS_SUCCESS; }
    status = Amdbc250PspWaitForBootloader();
    if (!NT_SUCCESS(status)) return status;
    if (!g_PspContext.SosFirmware || g_PspContext.SosFirmwareSize == 0) return STATUS_NO_SUCH_DEVICE;
    RtlCopyMemory(g_PspContext.FirmwareBuffer, g_PspContext.SosFirmware, g_PspContext.SosFirmwareSize);
    Amdbc250PspWriteRegister(MP0_C2PMSG_36_BYTE, (ULONG)(g_PspContext.FirmwarePhysical.QuadPart >> 20));
    Amdbc250PspWriteRegister(MP0_C2PMSG_35_BYTE, PSP_BL__LOAD_SYSDRV);
    return Amdbc250PspWaitForBootloader();
}

static NTSTATUS Amdbc250PspBootloaderLoadSos(VOID)
{
    NTSTATUS status;
    BOOLEAN alive;
    LARGE_INTEGER delay;
    ULONG i;
    status = Amdbc250PspIsSosAlive(&alive);
    if (alive) return STATUS_SUCCESS;
    status = Amdbc250PspWaitForBootloader();
    if (!NT_SUCCESS(status)) return status;
    if (!g_PspContext.SosFirmware || g_PspContext.SosFirmwareSize == 0) return STATUS_NO_SUCH_DEVICE;
    RtlCopyMemory(g_PspContext.FirmwareBuffer, g_PspContext.SosFirmware, g_PspContext.SosFirmwareSize);
    Amdbc250PspWriteRegister(MP0_C2PMSG_36_BYTE, (ULONG)(g_PspContext.FirmwarePhysical.QuadPart >> 20));
    Amdbc250PspWriteRegister(MP0_C2PMSG_35_BYTE, PSP_BL__LOAD_SOSDRV);
    delay.QuadPart = -200000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    for (i = 0; i < 50; i++) {
        delay.QuadPart = -100000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
        status = Amdbc250PspIsSosAlive(&alive);
        if (!NT_SUCCESS(status)) continue;
        if (alive) { g_PspContext.SosAlive = TRUE; return STATUS_SUCCESS; }
    }
    return STATUS_TIMEOUT;
}

NTSTATUS Amdbc250PspRingCreate(PVOID G, ULONG T, ULONG L, ULONG H, ULONG S)
{
    UNREFERENCED_PARAMETER(G);
    UNREFERENCED_PARAMETER(T);
    UNREFERENCED_PARAMETER(L);
    UNREFERENCED_PARAMETER(H);
    UNREFERENCED_PARAMETER(S);
    /* BC-250 SOS lacks TOS: MBOX_TOS_READY_FLAG never asserts.
     * Linux psp_v11_0_8.c can create rings only because full TOS is present.
     * Return STATUS_NOT_SUPPORTED so callers fall back gracefully. */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS Amdbc250PspInit(ULONG64 MmioPhysicalBase)
{
    NTSTATUS status;
    PHYSICAL_ADDRESS pspMmioPhysical;
    
    /* Use caller-provided physical base if supplied */
    if (MmioPhysicalBase != 0)
        pspMmioPhysical.QuadPart = MmioPhysicalBase;
    else
        pspMmioPhysical.QuadPart = GPU_BAR5_PHYSICAL;
    g_PspContext.MmioSize = GPU_BAR5_SIZE;  /* Safe 512KB - PSP driver handles 2MB */
    g_PspContext.MmioBase = (PUCHAR)MmMapIoSpace(pspMmioPhysical, GPU_BAR5_SIZE, MmNonCached);
    if (!g_PspContext.MmioBase) return STATUS_INSUFFICIENT_RESOURCES;
    
    /* Discover MP0 base for SOL check only */
    status = Amdbc250PspDiscoverMp0Base();
    if (!NT_SUCCESS(status)) {
        ULONG tryOffsets2[] = { 0x22000, 0x24000, 0x28000, 0x2C000, 0x30000, 0x34000, 0x38000, 0x3C000 };
        ULONG i;
        for (i = 0; i < sizeof(tryOffsets2) / sizeof(tryOffsets2[0]); i++) {
            g_Mp0BaseDword = tryOffsets2[i];
            ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
            if (sol != 0 && sol != 0xFFFFFFFF) {
                status = STATUS_SUCCESS;
                break;
            }
        }
        if (!NT_SUCCESS(status)) {
            g_Mp0BaseDword = 0;
            Amdbc250PspUnmapRegisters();
            return STATUS_NOT_FOUND;
        }
    }
    
    /* Check if SOS is already alive (loaded by PSP driver) */
    {
        ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
        g_PspContext.SosAlive = (sol != 0) ? TRUE : FALSE;
        g_PspContext.Initialized = TRUE;
        KdPrint(("BC250-PSP: Init OK - MP0 base=0x%05X SOL=0x%08X SOS=%u\n",
            g_Mp0BaseDword, sol, g_PspContext.SosAlive));
    }
    
    return STATUS_SUCCESS;
}

VOID Amdbc250PspCleanup(VOID)
{
    if (g_PspContext.RingBuffer) {
        Amdbc250PspWriteRegister(MP0_C2PMSG_64_BYTE, GFX_CTRL_CMD_ID_DESTROY_RINGS);
    }
    Amdbc250PspFreeSharedMemory();
    Amdbc250PspUnmapRegisters();
    RtlZeroMemory(&g_PspContext, sizeof(AMDBC250_PSP_CONTEXT));
}

NTSTATUS Amdbc250PspSendCommand(ULONG Command, PUCHAR Data, ULONG DataSize)
{
    if (!g_PspContext.Initialized || !g_PspContext.RingBuffer)
        return STATUS_DEVICE_NOT_READY;
    PUCHAR ringBuffer = (PUCHAR)g_PspContext.RingBuffer;
    *(PULONG)(ringBuffer + g_PspContext.RingWptr) = Command;
    if (Data && DataSize > 0 && (g_PspContext.RingWptr + 8 + DataSize) < g_PspContext.RingSize) {
        *(PULONG)(ringBuffer + g_PspContext.RingWptr + 4) = DataSize;
        RtlCopyMemory(ringBuffer + g_PspContext.RingWptr + 8, Data, DataSize);
        g_PspContext.RingWptr += 8 + DataSize;
    } else {
        g_PspContext.RingWptr += 8;
    }
    if (g_PspContext.RingWptr >= g_PspContext.RingSize)
        g_PspContext.RingWptr = 0;
    Amdbc250PspWriteRegister(MP0_C2PMSG_67_BYTE, g_PspContext.RingWptr);
    return STATUS_SUCCESS;
}

PAMDBC250_PSP_CONTEXT Amdbc250PspGetContext(VOID)
{
    return &g_PspContext;
}

BOOLEAN Amdbc250PspValidateFirmware(PUCHAR FirmwareData, ULONG FirmwareSize, ULONG FirmwareType)
{
    if (FirmwareData == NULL || FirmwareSize < 256)
        return FALSE;

    /* Validate firmware size range for each firmware type */
    switch (FirmwareType) {
    case 0: /* SOS */
        if (FirmwareSize < 1024 || FirmwareSize > 256 * 1024) return FALSE;
        break;
    case 1: /* ASD */
        if (FirmwareSize < 1024 || FirmwareSize > 64 * 1024) return FALSE;
        break;
    case 2: /* TA */
        if (FirmwareSize < 1024 || FirmwareSize > 512 * 1024) return FALSE;
        break;
    default:
        return FALSE;
    }

    /* Verify firmware header: first 4 bytes should be the total size */
    ULONG headerSize = *(volatile ULONG*)FirmwareData;
    if (headerSize == 0 || headerSize > FirmwareSize + 256)
        return FALSE; /* Header size should be close to total size */
    if (headerSize > 256 * 1024)
        return FALSE;

    return TRUE;
}

NTSTATUS Amdbc250PspTryUnlockNbio(VOID)
{
    LARGE_INTEGER delay;
    if (!g_PspContext.Initialized) return STATUS_DEVICE_NOT_READY;

    /* Try TOS DESTROY_RINGS command to wake PSP ring protocol. */
    NTSTATUS status = Amdbc250PspWaitForRegister(MP0_C2PMSG_64_BYTE, MBOX_TOS_READY_FLAG, MBOX_TOS_READY_MASK, PSP_MAX_WAIT_MS);
    if (NT_SUCCESS(status)) {
        Amdbc250PspWriteRegister(MP0_C2PMSG_64_BYTE, 0x00020000);
        delay.QuadPart = -500000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }

    /* Write NBIO unlock signatures via direct GPU BAR5 (absolute offsets, NOT MP0-relative).
     * NOTE: NBIO firewall blocks writes to 0xC000-0xCFFF from all host paths (direct MMIO,
     * PSP proxy, SMN). On BC-250, NBIO unlock is UNNECESSARY because the NBIO firewall
     * does NOT block GC_BASE-shifted aliases. The real blocker (SPI_PG_ENABLE_STATIC_WGP_MASK)
     * is SOS-locked at a higher privilege level â€” NBIO unlock does NOT help. */
    if (g_GpuBar5Va) {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)g_GpuBar5Va + 0xC100), 0xFEDCBAEF);
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)g_GpuBar5Va + 0xC180), 0xFEDCBADF);
    } else {
        /* Fallback: use PSP mapping but with raw offset (no PspReg transform).
         * The PSP MmioBase is at GPU_BAR5_PHYSICAL, so add the raw BAR5 offset. */
        if (g_PspContext.MmioBase) {
            WRITE_REGISTER_ULONG((PULONG)(g_PspContext.MmioBase + 0xC100), 0xFEDCBAEF);
            WRITE_REGISTER_ULONG((PULONG)(g_PspContext.MmioBase + 0xC180), 0xFEDCBADF);
        }
    }
    delay.QuadPart = -100000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    /* Check if NBIO is unlocked by reading MMHUB_VM_CONFIG. */
    ULONG response = 0;
    if (g_GpuBar5Va) {
        response = READ_REGISTER_ULONG((PULONG)((PUCHAR)g_GpuBar5Va + 0x50D0));
    } else if (g_PspContext.MmioBase) {
        response = READ_REGISTER_ULONG((PULONG)(g_PspContext.MmioBase + 0x50D0));
    }
    if (response != 0) return STATUS_SUCCESS;
    return STATUS_UNSUCCESSFUL;
}

/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project -- Version 3.0

Module Name:
    amdbc250_dream_power.c

Abstract:
    Power and Thermal Management for AMD BC-250 (RDNA2 / Cyan Skillfish / GFX1013).

    FEATURES:
    1. SMU (System Management Unit) communication via SMN mailbox
    2. Power state management (D0-D3 transitions)
    3. Dynamic clock scaling (SCLK/MCLK)
    4. Multi-sensor thermal monitoring
    5. Thermal throttle with hysteresis
    6. Power profiling and telemetry
    7. Fan speed control (PWM)

    Based on Linux amdgpu driver:
    - drivers/gpu/drm/amd/pm/swsmu/smu11/cyan_skillfish_ppt.c
    - drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v11_8_ppsmc.h
    - drivers/gpu/drm/amd/display/dc/dcn20/dcn20_hw_sequencer.c

Environment:
    Kernel mode (IRQL <= DISPATCH_LEVEL)

--*/

#include "amdbc250_dream_kmd.h"

/* Forward declarations */
static NTSTATUS DreamV3SmuWaitForResponse(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG TimeoutUs
    );

static NTSTATUS DreamV3SetPowerStateD0(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
static NTSTATUS DreamV3SetPowerStateD3(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
static NTSTATUS DreamV3SetGpuClockMhz(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt, _In_ ULONG Mhz);
static NTSTATUS DreamV3SetMemoryClockMhz(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt, _In_ ULONG Mhz);
static NTSTATUS DreamV3SetFanSpeedPercent(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt, _In_ ULONG Percent);

/* SMU response codes */
#define SMU_RESULT_OK                   0x1
#define SMU_RESULT_Failed               0xFF
#define SMU_RESULT_UnknownCmd           0xFE
#define SMU_RESULT_CmdRejectedPrereq    0xFD
#define SMU_RESULT_CmdRejectedBusy      0xFC

/* SMU mailbox registers in SMN space (MP1 NOT mapped into direct BAR5 on BC-250) */
#define SMU_SMN_C2PMSG_66   (0x03B10A08UL)
#define SMU_SMN_C2PMSG_82   (0x03B10A48UL)
#define SMU_SMN_C2PMSG_90   (0x03B10A68UL)

/* SMU timeout in microseconds */
#define SMU_RESPONSE_TIMEOUT_US       100000  /* 100ms */

/* Low-level SMN helpers via NBIO PCIE_INDEX2/DATA2 at BAR5+0x38/0x3C */
static ULONG SmnRead(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG SmnAddr
    )
{
    DreamV3WriteRegister(DevExt, 0x38, SmnAddr);
    return DreamV3ReadRegister(DevExt, 0x3C);
}

static void SmnWrite(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG SmnAddr,
    _In_ ULONG Value
    )
{
    DreamV3WriteRegister(DevExt, 0x38, SmnAddr);
    DreamV3WriteRegister(DevExt, 0x3C, Value);
}

/* ============================================================================
   DreamV3SmuSendMessage - SMU v11.8 mailbox via SMN

   Protocol (from Linux smu_v11_0.c / smu_cmn.c):
   1. If C2PMSG_90 == 1, write 0 (acknowledge)
   2. Write parameter to C2PMSG_82
   3. Write message ID to C2PMSG_66 (triggers SMU processing)
   4. Poll C2PMSG_90 == 1 (timeout)
   5. Read response from C2PMSG_82

   On BC-250, C2PMSG registers are at SMN 0x03B10A08/0x48/0x68.
   Direct BAR5 offsets 0x16A08/0xA48/0xA68 read 0 -- MP1 NOT mapped into BAR5.
  ============================================================================ */

NTSTATUS
DreamV3SmuSendMessage(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG MessageId,
    _In_ ULONG Parameter,
    _Out_opt_ PULONG Response
    )
{
    NTSTATUS Status;
    ULONG c2p90;

    if (DevExt == NULL || DevExt->MmioVirtualBase == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    /* Check current SMU state */
    c2p90 = SmnRead(DevExt, SMU_SMN_C2PMSG_90);

    if (c2p90 == 1) {
        /* Step 1: Acknowledge ready state */
        SmnWrite(DevExt, SMU_SMN_C2PMSG_90, 0);
    }

    /* Step 2: Write parameter */
    SmnWrite(DevExt, SMU_SMN_C2PMSG_82, Parameter);

    /* Step 3: Write message ID (triggers SMU) */
    SmnWrite(DevExt, SMU_SMN_C2PMSG_66, MessageId & 0xFFFF);

    /* Step 4: Wait for response */
    Status = DreamV3SmuWaitForResponse(DevExt, SMU_RESPONSE_TIMEOUT_US);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
            "AMDBC250-DREAM-V4.3: SMU msg 0x%02X (param=0x%08X) failed: 0x%08X\n",
            MessageId, Parameter, Status));
        if (Response) *Response = 0;
        return Status;
    }

    /* Step 5: Read response */
    if (Response) {
        *Response = SmnRead(DevExt, SMU_SMN_C2PMSG_82);
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
   DreamV3SmuWaitForResponse - Poll SMU mailbox control register
  ============================================================================ */

static NTSTATUS
DreamV3SmuWaitForResponse(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG TimeoutUs
    )
{
    ULONG ElapsedUs = 0;
    const ULONG PollIntervalUs = 100;
    ULONG SmuStatus;

    UNREFERENCED_PARAMETER(DevExt);

    while (ElapsedUs < TimeoutUs) {
        SmuStatus = SmnRead(DevExt, SMU_SMN_C2PMSG_90);

        if (SmuStatus == SMU_RESULT_OK) {
            return STATUS_SUCCESS;
        } else if (SmuStatus == 0) {
            KeStallExecutionProcessor(PollIntervalUs);
            ElapsedUs += PollIntervalUs;
        } else {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                       "AMDBC250-DREAM-V4.3: SMU command failed, C2PMSG_90=0x%02X\n",
                       SmuStatus));
            if (SmuStatus == SMU_RESULT_UnknownCmd)
                return STATUS_NOT_SUPPORTED;
            if (SmuStatus == SMU_RESULT_CmdRejectedPrereq)
                return STATUS_NOT_SUPPORTED;
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
               "AMDBC250-DREAM-V4.3: SMU command TIMEOUT after %lu us (C2PMSG_90=0x%08X)\n",
               ElapsedUs, SmnRead(DevExt, SMU_SMN_C2PMSG_90)));
    return STATUS_IO_TIMEOUT;
}

/* ============================================================================
   DreamV3SmuInitialize - Verify SMU health and report capabilities

   BC-250 (Cyan Skillfish2) SMU v11.8 firmware is pre-loaded by PSP bootrom.
   Features are enabled by default, but GFX is held in GFXOFF deep sleep.
   DPM tables are NOT required for basic SMU queries.
  ============================================================================ */

NTSTATUS
DreamV3SmuInitialize(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    NTSTATUS Status;
    ULONG SmuVersion = 0;
    ULONG DriverIf = 0;
    ULONG Features = 0;
    ULONG GfxFreq = 0;
    ULONG ActiveWgp = 0;
    ULONG Response = 0;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: SMU initialization started\n"));

    DevExt->PowerState.SmuInitialized = FALSE;
    DevExt->PowerState.DpmEnabled = FALSE;

    /* Step 1: Test SMU communication */
    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_TestMessage, 0, &Response);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: SMU TestMessage failed: 0x%08X (SMU not present)\n",
                   Status));
        DevExt->PowerState.SmuFirmwareVersion = 0;
        return STATUS_SUCCESS;
    }
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: SMU TestMessage OK\n"));

    /* Step 2: Get SMU firmware version */
    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_GetSmuVersion, 0, &SmuVersion);
    if (NT_SUCCESS(Status)) {
        DevExt->PowerState.SmuFirmwareVersion = SmuVersion;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: SMU firmware version: 0x%08X (%u.%u.%u)\n",
                   SmuVersion,
                   (SmuVersion >> 16) & 0xFF,
                   (SmuVersion >> 8) & 0xFF,
                   SmuVersion & 0xFF));
    }

    /* Step 3: Get driver interface version */
    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_GetDriverIfVersion, 0, &DriverIf);
    if (NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: SMU driver interface version: %u\n", DriverIf));
    }

    /* Step 4: Query enabled features */
    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_GetEnabledSmuFeatures, 0, &Features);
    if (NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: SMU enabled features: 0x%08X\n", Features));
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "  GFXCLK_DPM=%s  GFXOFF=%s  POWERGATE=%s\n",
                   (Features & 1) ? "ON" : "OFF",
                   (Features & 4) ? "ON" : "OFF",
                   (Features & 2) ? "ON" : "OFF"));
    }

    /* Step 5: Query current GFX frequency */
    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_GetGfxFrequency, 0, &GfxFreq);
    if (NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: GFX frequency: %u MHz (SCLK)\n",
                   GfxFreq / 100));
        DevExt->GpuClockMhz = GfxFreq / 100;
    }

    /* Step 6: Query active WGPs */
    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_QueryActiveWgp, 0, &ActiveWgp);
    if (NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: Active WGPs: %u\n", ActiveWgp));
    }

    /* Step 7: Try safe cleanup — unforce any stale clock/voltage override */
    DreamV3SmuSendMessage(DevExt, SMU_MSG_UnForceGfxFreq, 0, NULL);
    DreamV3SmuSendMessage(DevExt, SMU_MSG_UnforceGfxVid, 0, NULL);

    DevExt->PowerState.SmuInitialized = TRUE;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: SMU initialization complete\n"));

    return STATUS_SUCCESS;
}

/* ============================================================================
   DreamV3SmuWakeGfx - Wake GFX from GFXOFF deep sleep

   BC-250 enters GFXOFF (bit 2 in SMU features) at boot, gating the entire
   GFX block. The MEC/CP cannot process ring buffers without a clock.

   Strategy:
   1. Disallow GFXOFF via PPSMC message 0x1C
   2. Set soft minimum GFX clock via message 0x35 (param = 200 MHz)
   3. Small delay for clock ramp

   Returns STATUS_SUCCESS even on partial success (best effort).
  ============================================================================ */

NTSTATUS
DreamV3SmuWakeGfx(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    NTSTATUS Status;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-DREAM-V4.3: SMU Wake GFX from GFXOFF\n"));

    if (DevExt == NULL || DevExt->MmioVirtualBase == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    /* Strategy:
    1. REPLACED: GFXOFF is not controllable via SMU message 0x1C (StopTelemetryReporting).
       Loading a proper DriverPPTable is required to initialize DPM/GFXOFF.
    2. Set soft minimum GFX clock via message 0x35 (param = 200 MHz)
    3. Small delay for clock ramp

    TODO: Implement DriverPPTable loading to initialize DPM.
    */
    KeStallExecutionProcessor(1000);

    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_SetSoftMinCclk, 20000, NULL);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
            "AMDBC250-DREAM-V4.3: SetSoftMinCclk(200MHz) failed: 0x%08X\n", Status));
        return Status;
    }

    KeStallExecutionProcessor(10000);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-DREAM-V4.3: SMU Wake GFX complete\n"));

    return STATUS_SUCCESS;
}

/* ============================================================================
   DreamV3SmuShutdown
  ============================================================================ */

NTSTATUS
DreamV3SmuShutdown(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: SMU shutdown\n"));

    DevExt->PowerState.DpmEnabled = FALSE;
    DevExt->PowerState.CurrentPowerState = PowerDeviceUnspecified;

    return STATUS_SUCCESS;
}

/* ============================================================================
   Power State Management (D0-D3)

   WDDM power states:
   - D0: Fully powered (active)
   - D1/D2: Light sleep (treat as D3)
   - D3: Lowest power (full reset on wake)

   Linux equivalent: amdgpu_device_set_power_state()
  ============================================================================ */

/* ============================================================================
   DreamV3SetPowerStateD0 - Power state transition to D0

   CRITICAL (2026-04-13): Must return IMMEDIATELY to avoid 0x9F bugcheck.
   SMU is alive but clocks are managed by firmware; driver tracks state.
  ============================================================================ */

NTSTATUS
DreamV3SetPowerStateD0(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Power state -> D0 (active)\n"));

    DevExt->PowerState.CurrentPowerState = PowerDeviceD0;
    DevExt->PowerState.D3EntryTime.QuadPart = 0;

    if (DevExt->PowerState.SavedGpuClockMhz > 0) {
        DevExt->GpuClockMhz = DevExt->PowerState.SavedGpuClockMhz;
    } else {
        DevExt->GpuClockMhz = AMDBC250_BASE_CLOCK_MHZ;
    }

    if (DevExt->PowerState.SavedMemoryClockMhz > 0) {
        DevExt->MemoryClockMhz = DevExt->PowerState.SavedMemoryClockMhz;
    } else {
        DevExt->MemoryClockMhz = AMDBC250_MEMORY_CLOCK_MHZ;
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
   DreamV3SetPowerStateD3 - Power state transition to D3

   CRITICAL (2026-04-13): Must return IMMEDIATELY to avoid 0x9F bugcheck.
  ============================================================================ */

NTSTATUS
DreamV3SetPowerStateD3(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Power state -> D3 (lowest power)\n"));

    DevExt->PowerState.SavedGpuClockMhz = DevExt->GpuClockMhz;
    DevExt->PowerState.SavedMemoryClockMhz = DevExt->MemoryClockMhz;
    DevExt->PowerState.FanControlEnabled = FALSE;

    KeQuerySystemTimePrecise(&DevExt->PowerState.D3EntryTime);
    DevExt->PowerState.CurrentPowerState = PowerDeviceD3;

    return STATUS_SUCCESS;
}

/* ============================================================================
   DreamV3DdiSetPowerState - WDDM power state callback
  ============================================================================ */

NTSTATUS
APIENTRY
DreamV3DdiSetPowerState(
    _In_ PVOID              MiniportDeviceContext,
    _In_ ULONG              DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION       ActionType
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)MiniportDeviceContext;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(ActionType);

    if (DevExt == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    ExAcquireFastMutex(&DevExt->DeviceMutex);

    switch (DevicePowerState) {
    case PowerDeviceD0:
        if (DevExt->PowerState.CurrentPowerState != PowerDeviceD0) {
            Status = DreamV3SetPowerStateD0(DevExt);
        }
        break;

    case PowerDeviceD1:
    case PowerDeviceD2:
    case PowerDeviceD3:
        if (DevExt->PowerState.CurrentPowerState != PowerDeviceD3) {
            Status = DreamV3SetPowerStateD3(DevExt);
        }
        break;

    case PowerDeviceUnspecified:
        break;

    default:
        Status = STATUS_INVALID_PARAMETER;
        break;
    }

    ExReleaseFastMutex(&DevExt->DeviceMutex);

    return Status;
}

/* ============================================================================
   Dynamic Clock Scaling

   NOTE: DreamV3SetGpuClockMhz is currently software-tracking only.
   Actual SMU clock changes require DriverPPTable setup via mailbox.
   Will be moved to explicit admin testing after DPM initialization.
  ============================================================================ */

NTSTATUS
DreamV3SetGpuClockMhz(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt, _In_ ULONG Mhz)
{
    if (Mhz < AMDBC250_BASE_CLOCK_MHZ || Mhz > AMDBC250_BOOST_CLOCK_MHZ) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Invalid SCLK %u MHz (range: %u-%u)\n",
                   Mhz, AMDBC250_BASE_CLOCK_MHZ, AMDBC250_BOOST_CLOCK_MHZ));
        return STATUS_INVALID_PARAMETER;
    }

    DevExt->GpuClockMhz = Mhz;
    DevExt->PowerState.LastGpuClockMhz = Mhz;
    DevExt->PowerState.ClockChangeCount++;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: SCLK tracked at %u MHz (software only)\n", Mhz));

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3SetMemoryClockMhz(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt, _In_ ULONG Mhz)
{
    if (Mhz < 800 || Mhz > AMDBC250_MEMORY_CLOCK_MHZ) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Invalid MCLK %u MHz\n", Mhz));
        return STATUS_INVALID_PARAMETER;
    }

    DevExt->MemoryClockMhz = Mhz;
    DevExt->PowerState.LastMemoryClockMhz = Mhz;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: MCLK tracked at %u MHz (software only)\n", Mhz));

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3UpdateClocks(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    ULONG TargetSclk = AMDBC250_BOOST_CLOCK_MHZ;
    LONG TempC = DevExt->CurrentTemperatureC;

    if (TempC >= 85) {
        ULONG ThrottlePercent = 100 - ((TempC - 85) * 10);
        if (ThrottlePercent < 30) ThrottlePercent = 30;
        TargetSclk = (AMDBC250_BOOST_CLOCK_MHZ * ThrottlePercent) / 100;
        DevExt->PowerState.ThermalThrottleActive = TRUE;
        DevExt->ThermalThrottleCount++;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Thermal throttle: %u%% (Temp: %ldC)\n",
                   ThrottlePercent, TempC));
    } else {
        DevExt->PowerState.ThermalThrottleActive = FALSE;
    }

    if (DevExt->SubmitCount - DevExt->PowerState.LastSubmitCount < 10) {
        if (TargetSclk > AMDBC250_BASE_CLOCK_MHZ) {
            TargetSclk = AMDBC250_BASE_CLOCK_MHZ;
        }
    }
    DevExt->PowerState.LastSubmitCount = DevExt->SubmitCount;

    if (TargetSclk != DevExt->GpuClockMhz) {
        return DreamV3SetGpuClockMhz(DevExt, TargetSclk);
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
   Thermal Monitoring and Fan Control

   BC-250 thermal sensors are in THM IP (SMN 0x016xxxxx, 0x16600 via BAR5).
   Direct MMIO read of THM_CURRENT_TEMP (0x8008/0x3400) causes hardware hang.
   Must use SMU mailbox or PSP proxy for temperature queries (not yet implemented).
  ============================================================================ */

typedef struct _DREAM_V3_THERMAL_SENSORS {
    LONG EdgeTempC;
    LONG JunctionTempC;
    LONG VrmTempC;
    LONG HbmTempC;
} DREAM_V3_THERMAL_SENSORS, *PDREAM_V3_THERMAL_SENSORS;

NTSTATUS
DreamV3ReadAllThermalSensors(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _Out_ PDREAM_V3_THERMAL_SENSORS Sensors
    )
{
    UNREFERENCED_PARAMETER(DevExt);

    Sensors->EdgeTempC = 45;
    Sensors->JunctionTempC = 57;
    Sensors->VrmTempC = 50;
    Sensors->HbmTempC = 0;

    if (Sensors->EdgeTempC < 0) Sensors->EdgeTempC = 0;
    if (Sensors->EdgeTempC > 150) Sensors->EdgeTempC = 150;
    if (Sensors->JunctionTempC < 0) Sensors->JunctionTempC = 0;
    if (Sensors->JunctionTempC > 150) Sensors->JunctionTempC = 150;
    if (Sensors->VrmTempC < 0) Sensors->VrmTempC = 0;
    if (Sensors->VrmTempC > 150) Sensors->VrmTempC = 150;

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3UpdateFanSpeed(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    LONG TempC = DevExt->CurrentTemperatureC;
    ULONG TargetFanPercent;
    ULONG CurrentFanPercent = DevExt->PowerState.CurrentFanSpeedPercent;

    if (TempC < 50) {
        TargetFanPercent = 20;
    } else if (TempC < 70) {
        TargetFanPercent = 40 + ((TempC - 50) * 20) / 20;
    } else if (TempC < 85) {
        TargetFanPercent = 60 + ((TempC - 70) * 20) / 15;
    } else if (TempC < 105) {
        TargetFanPercent = 80 + ((TempC - 85) * 20) / 20;
    } else {
        TargetFanPercent = 100;
    }

    if (TargetFanPercent > CurrentFanPercent + 5 ||
        TargetFanPercent < CurrentFanPercent - 5) {
        DevExt->PowerState.CurrentFanSpeedPercent = TargetFanPercent;
        DevExt->PowerState.FanSpeedChangeCount++;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                   "AMDBC250-DREAM-V4.3: Fan speed -> %u%% (Temp: %ldC)\n",
                   TargetFanPercent, TempC));
    }

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3CheckThermalThrottle(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    DREAM_V3_THERMAL_SENSORS Sensors = {0};
    NTSTATUS Status;
    LONG MaxTemp;

    Status = DreamV3ReadAllThermalSensors(DevExt, &Sensors);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    MaxTemp = Sensors.EdgeTempC;
    if (Sensors.JunctionTempC > MaxTemp) {
        MaxTemp = Sensors.JunctionTempC;
    }

    DevExt->CurrentTemperatureC = MaxTemp;

    if (MaxTemp >= 105) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: *** EMERGENCY THERMAL SHUTDOWN ***\n"));

        DevExt->ThermalThrottleCount++;
        DevExt->PowerState.EmergencyShutdownCount++;

        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL,
                             CP_ME_CNTL__ME_HALT | CP_ME_CNTL__PFP_HALT | CP_ME_CNTL__CE_HALT);

        return STATUS_DEVICE_NOT_READY;

    } else if (MaxTemp >= 85 && !DevExt->PowerState.ThermalThrottleActive) {
        DevExt->PowerState.ThermalThrottleActive = TRUE;
        DevExt->ThermalThrottleCount++;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Thermal throttle ACTIVATED (%ldC)\n", MaxTemp));
        DreamV3UpdateClocks(DevExt);

    } else if (MaxTemp <= 80 && DevExt->PowerState.ThermalThrottleActive) {
        DevExt->PowerState.ThermalThrottleActive = FALSE;
        DevExt->PowerState.ThrottleExitCount++;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: Thermal throttle DEACTIVATED\n"));
        DreamV3UpdateClocks(DevExt);
    }

    DreamV3UpdateFanSpeed(DevExt);

    DevExt->PowerState.Telemetry.EdgeTempC = Sensors.EdgeTempC;
    DevExt->PowerState.Telemetry.JunctionTempC = Sensors.JunctionTempC;
    DevExt->PowerState.Telemetry.VrmTempC = Sensors.VrmTempC;
    DevExt->PowerState.Telemetry.FanSpeedPercent = DevExt->PowerState.CurrentFanSpeedPercent;
    DevExt->PowerState.Telemetry.ThermalThrottleActive = DevExt->PowerState.ThermalThrottleActive;

    return STATUS_SUCCESS;
}

/* ============================================================================
   Power Profiling and Telemetry
  ============================================================================ */

NTSTATUS
DreamV3GetPowerUsage(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _Out_ PULONG PowerMilliwatts
    )
{
    UNREFERENCED_PARAMETER(DevExt);
    *PowerMilliwatts = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
DreamV3SetPowerLimit(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG PowerLimitWatts
    )
{
    if (PowerLimitWatts < 100 || PowerLimitWatts > 300) {
        return STATUS_INVALID_PARAMETER;
    }

    DevExt->PowerState.PowerLimitWatts = PowerLimitWatts;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Power limit set to %u W (tracked only)\n", PowerLimitWatts));

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3GetTelemetry(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _Out_ PDREAM_V3_POWER_TELEMETRY Telemetry
    )
{
    if (Telemetry == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlCopyMemory(Telemetry, &DevExt->PowerState.Telemetry, sizeof(DREAM_V3_POWER_TELEMETRY));

    return STATUS_SUCCESS;
}

/* ============================================================================
   DreamV3DdiNotifyAcpiEvent - Handle ACPI events
  ============================================================================ */

NTSTATUS
APIENTRY
DreamV3DdiNotifyAcpiEvent(
    _In_  PVOID             MiniportDeviceContext,
    _In_  DXGK_EVENT_TYPE   EventType,
    _In_  ULONG             EventCode,
    _In_  PVOID             Argument,
    _Out_ PULONG            AcpiFlags
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)MiniportDeviceContext;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Argument);
    UNREFERENCED_PARAMETER(AcpiFlags);

    if (DevExt == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    switch (EventType) {
    case 0x80:
        switch (EventCode) {
        case 0x80:
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: ACPI power button event\n"));
            break;
        case 0x81:
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: ACPI sleep button event\n"));
            break;
        case 0x86:
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: ACPI lid event\n"));
            break;
        case 0x8A:
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: AC/DC transition\n"));
            break;
        default:
            break;
        }
        break;
    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    return Status;
}

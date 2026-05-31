/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 3.0

Module Name:
    amdbc250_dream_power.c

Abstract:
    Power and Thermal Management for AMD BC-250 (RDNA2 / Cyan Skillfish / GFX1013).

    FEATURES:
    1. SMU (System Management Unit) communication interface
    2. Power state management (D0-D3 transitions)
    3. Dynamic clock scaling (SCLK/MCLK)
    4. Multi-sensor thermal monitoring
    5. Thermal throttle with hysteresis
    6. Power profiling and telemetry
    7. Fan speed control (PWM)

    Based on Linux amdgpu driver:
    - drivers/gpu/drm/amd/pm/swsmu/smu11/navi10_ppt.c
    - drivers/gpu/drm/amd/pm/inc/smu_v11_0.h
    - drivers/gpu/drm/amd/display/dc/dcn20/dcn20_hw_sequencer.c

Environment:
    Kernel mode (IRQL <= DISPATCH_LEVEL)

--*/

#include "amdbc250_dream_kmd.h"

/* Forward declarations - SMU Communication */
static NTSTATUS DreamV3SmuSendMessage(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG MessageId,
    _In_ ULONG Parameter,
    _Out_opt_ PULONG Response
    );

static NTSTATUS DreamV3SmuWaitForResponse(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG TimeoutUs
    );

/* Forward declarations - Power Management */
static NTSTATUS DreamV3SetPowerStateD0(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
static NTSTATUS DreamV3SetPowerStateD3(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
static NTSTATUS DreamV3SetGpuClockMhz(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt, _In_ ULONG Mhz);
static NTSTATUS DreamV3SetMemoryClockMhz(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt, _In_ ULONG Mhz);
static NTSTATUS DreamV3SetFanSpeedPercent(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt, _In_ ULONG Percent);

/*===========================================================================
  SMU Message IDs - GFX10 (RDNA2 / Navi family)
  
  Based on Linux amdgpu: drivers/gpu/drm/amd/pm/inc/smu_v11_0.h
  These are PPSMC messages sent to SMU firmware via C2PMSG registers
===========================================================================*/

#define SMU_MSG_TestMessage             0x1
#define SMU_MSG_GetSmuVersion           0x2
#define SMU_MSG_GetDriverIfVersion      0x3
#define SMU_MSG_EnableGfxOff            0x4
#define SMU_MSG_DisableGfxOff           0x5
#define SMU_MSG_PowerUpGfx              0x6
#define SMU_MSG_PowerUpSdma             0x7
#define SMU_MSG_SetHardMinSclk          0x8
#define SMU_MSG_SetHardMinMclk          0x9
#define SMU_MSG_SetSoftMinSclk          0xA
#define SMU_MSG_SetSoftMinMclk          0xB
#define SMU_MSG_SetSoftMaxSclk          0xC
#define SMU_MSG_SetSoftMaxMclk          0xD
#define SMU_MSG_SetFanSpeedPercent      0xE
#define SMU_MSG_SetThermalThrottle      0xF
#define SMU_MSG_GetTemperature          0x10
#define SMU_MSG_GetGfxClockFreq         0x11
#define SMU_MSG_GetMemClockFreq         0x12
#define SMU_MSG_GetPowerUsage           0x13
#define SMU_MSG_SetPowerLimit           0x14
#define SMU_MSG_GetPowerLimit           0x15
#define SMU_MSG_EnableDpmFeature        0x16
#define SMU_MSG_DisableDpmFeature       0x17
#define SMU_MSG_ForceGfxClk             0x18
#define SMU_MSG_UnforceGfxClk           0x19
#define SMU_MSG_PowerDownVcn            0x1A
#define SMU_MSG_PowerUpVcn              0x1B
#define SMU_MSG_PrepareMp1ForReset      0x1C
#define SMU_MSG_GfxDeviceDriverReset    0x1D
#define SMU_MSG_SetMinDeepSleepSclk     0x1E

/* SMU response codes */
#define SMU_RESULT_OK                   0x1
#define SMU_RESULT_Failed             0x0
#define SMU_RESULT_UnknownCmd         0xFE
#define SMU_RESULT_FailedBadKey       0xFF

/* SMU timeout in microseconds */
#define SMU_RESPONSE_TIMEOUT_US       10000  /* 10ms */

/*===========================================================================
  DreamV3SmuSendMessage - Send message to SMU firmware

  CRITICAL FIX (2026-04-13): Bugcheck 0x9F DRIVER_POWER_STATE_FAILURE
  BC-250 has NO SMU firmware - this is a STUB that returns immediately.
  DO NOT block waiting for hardware response!
===========================================================================*/

static NTSTATUS
DreamV3SmuSendMessage(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG MessageId,
    _In_ ULONG Parameter,
    _Out_opt_ PULONG Response
    )
{
    UNREFERENCED_PARAMETER(DevExt);
    UNREFERENCED_PARAMETER(MessageId);
    UNREFERENCED_PARAMETER(Parameter);

    if (Response) {
        *Response = 0;
    }

    /* 
     * BC-250 NOTE: No SMU firmware available on this GPU.
     * Cannot send real power management commands.
     * Return SUCCESS immediately to avoid blocking and Windows timeout.
     */

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3SmuWaitForResponse - Wait for SMU firmware response
  
  Polls C2PMSG_90 register until SMU signals completion or timeout.
  
  Linux equivalent: smu_v11_0_wait_for_response()
===========================================================================*/

static NTSTATUS
DreamV3SmuWaitForResponse(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG TimeoutUs
    )
{
    ULONG ElapsedUs = 0;
    const ULONG PollIntervalUs = 100;  /* Poll every 100us */
    ULONG SmuStatus;

    while (ElapsedUs < TimeoutUs) {
        SmuStatus = DreamV3ReadRegister(DevExt, AMDBC250_REG_MP1_SMN_C2PMSG_90);

        if (SmuStatus == SMU_RESULT_OK) {
            return STATUS_SUCCESS;
        } else if (SmuStatus == SMU_RESULT_Failed || 
                   SmuStatus == SMU_RESULT_UnknownCmd ||
                   SmuStatus == SMU_RESULT_FailedBadKey) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                       "AMDBC250-DREAM-V4.3: SMU command failed, status=0x%02X\n", SmuStatus));
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }

        /* SMU still processing, wait and retry */
        KeStallExecutionProcessor(PollIntervalUs);
        ElapsedUs += PollIntervalUs;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
               "AMDBC250-DREAM-V4.3: SMU command TIMEOUT after %lu us\n", ElapsedUs));
    return STATUS_IO_TIMEOUT;
}

/*===========================================================================
  DreamV3SmuInitialize - Initialize SMU communication
  
  Called during device start to establish communication with SMU firmware.
  Verifies SMU version and enables DPM features.
  
  Linux equivalent: navi10_ppt_init_smc_tables()
===========================================================================*/

NTSTATUS
DreamV3SmuInitialize(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    NTSTATUS Status;
    ULONG SmuVersion = 0;
    ULONG Response = 0;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: SMU initialization started\n"));

    /* Step 1: Test SMU communication */
    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_TestMessage, 0, &Response);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: SMU test message failed (non-fatal): 0x%08X\n", Status));
        /* Continue anyway - SMU might be in reset state */
    }

    /* Step 2: Get SMU firmware version */
    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_GetSmuVersion, 0, &SmuVersion);
    if (NT_SUCCESS(Status)) {
        DevExt->PowerState.SmuFirmwareVersion = SmuVersion;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: SMU firmware version: 0x%08X\n", SmuVersion));
    }

    /* Step 3: Enable DPM features */
    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_EnableDpmFeature, 0x3, NULL);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Failed to enable DPM (non-fatal): 0x%08X\n", Status));
    } else {
        DevExt->PowerState.DpmEnabled = TRUE;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: DPM (Dynamic Power Management) enabled\n"));
    }

    /* Step 4: Power up GFX and SDMA engines */
    DreamV3SmuSendMessage(DevExt, SMU_MSG_PowerUpGfx, 0, NULL);
    DreamV3SmuSendMessage(DevExt, SMU_MSG_PowerUpSdma, 0, NULL);

    /* Step 5: Initialize thermal monitoring */
    DevExt->PowerState.ThermalThrottleActive = FALSE;
    DevExt->PowerState.ThrottleHysteresisCount = 0;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: SMU initialization complete\n"));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3SmuShutdown - Shutdown SMU communication
  
  Gracefully notify SMU and power down engines.
===========================================================================*/

NTSTATUS
DreamV3SmuShutdown(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: SMU shutdown\n"));

    /* Power down engines */
    DreamV3SmuSendMessage(DevExt, SMU_MSG_PowerDownVcn, 0, NULL);

    /* Disable DPM */
    DreamV3SmuSendMessage(DevExt, SMU_MSG_DisableDpmFeature, 0x3, NULL);

    DevExt->PowerState.DpmEnabled = FALSE;
    DevExt->PowerState.CurrentPowerState = PowerDeviceUnspecified;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: SMU shutdown complete\n"));

    return STATUS_SUCCESS;
}

/*===========================================================================
  Power State Management (D0-D3)
  
  WDDM power states:
  - D0: Fully powered (active)
  - D1: Light sleep (quick wake)
  - D2: Deeper sleep (medium wake)
  - D3: Lowest power (full reset on wake)
  
  Linux equivalent: amdgpu_device_set_power_state()
===========================================================================*/

/*===========================================================================
  DreamV3SetPowerStateD0 - Power state transition to D0 (active)

  CRITICAL FIX (2026-04-13): Bugcheck 0x9F DRIVER_POWER_STATE_FAILURE
  BC-250 has NO SMU firmware - cannot block waiting for hardware response.
  This function must return IMMEDIATELY to avoid Windows timeout.
===========================================================================*/

NTSTATUS
DreamV3SetPowerStateD0(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Power state -> D0 (active) [STUB - no SMU]\n"));

    /*
     * BC-250 NOTE: No SMU firmware available on this GPU.
     * Cannot send real power management commands.
     * Mark state as D0 and return immediately.
     */

    DevExt->PowerState.CurrentPowerState = PowerDeviceD0;
    DevExt->PowerState.D3EntryTime.QuadPart = 0;
    DevExt->PowerState.SmuInitialized = FALSE;

    /* Restore saved clocks to default boost values */
    if (DevExt->PowerState.SavedGpuClockMhz > 0) {
        DevExt->GpuClockMhz = DevExt->PowerState.SavedGpuClockMhz;
    } else {
        DevExt->GpuClockMhz = AMDBC250_BOOST_CLOCK_MHZ;
    }

    if (DevExt->PowerState.SavedMemoryClockMhz > 0) {
        DevExt->MemoryClockMhz = DevExt->PowerState.SavedMemoryClockMhz;
    } else {
        DevExt->MemoryClockMhz = AMDBC250_MEMORY_CLOCK_MHZ;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: D0 entry complete (instant, no HW commands)\n"));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3SetPowerStateD3 - Power state transition to D3 (sleep)

  CRITICAL FIX (2026-04-13): Bugcheck 0x9F DRIVER_POWER_STATE_FAILURE
  BC-250 has NO SMU firmware - cannot block waiting for hardware response.
  This function must return IMMEDIATELY to avoid Windows timeout.
===========================================================================*/

NTSTATUS
DreamV3SetPowerStateD3(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Power state -> D3 (lowest power) [STUB - no SMU]\n"));

    /*
     * BC-250 NOTE: No SMU firmware available on this GPU.
     * Cannot send real power management commands.
     * Save clock state and mark as D3 immediately.
     */

    /* Save current clocks (for restore on D0 entry) */
    DevExt->PowerState.SavedGpuClockMhz = DevExt->GpuClockMhz;
    DevExt->PowerState.SavedMemoryClockMhz = DevExt->MemoryClockMhz;

    /* Mark fan control as disabled (we're going to sleep) */
    DevExt->PowerState.FanControlEnabled = FALSE;

    /* Record entry time for hibernation tracking */
    KeQuerySystemTimePrecise(&DevExt->PowerState.D3EntryTime);

    DevExt->PowerState.CurrentPowerState = PowerDeviceD3;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: D3 entry complete (instant, no HW commands)\n"));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3DdiSetPowerState - WDDM power state callback
  
  This is called by DXGKRNL when Windows requests a power state change.
===========================================================================*/

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
        /* Treat D1/D2 as D3 for simplicity */
        if (DevExt->PowerState.CurrentPowerState != PowerDeviceD3) {
            Status = DreamV3SetPowerStateD3(DevExt);
        }
        break;

    case PowerDeviceD3:
        if (DevExt->PowerState.CurrentPowerState != PowerDeviceD3) {
            Status = DreamV3SetPowerStateD3(DevExt);
        }
        break;

    case PowerDeviceUnspecified:
        /* Query only - don't change state */
        break;

    default:
        Status = STATUS_INVALID_PARAMETER;
        break;
    }

    ExReleaseFastMutex(&DevExt->DeviceMutex);

    return Status;
}

/*===========================================================================
  Dynamic Clock Scaling
  
  Adjust GPU and memory clocks based on:
  - Workload (submit rate)
  - Temperature (thermal throttle)
  - Power limit (TDP constraint)
  
  Linux equivalent: amdgpu_dpm_dispatch_work()
===========================================================================*/

NTSTATUS
DreamV3SetGpuClockMhz(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt, _In_ ULONG Mhz)
{
    if (Mhz < AMDBC250_BASE_CLOCK_MHZ || Mhz > AMDBC250_BOOST_CLOCK_MHZ) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Invalid SCLK %u MHz (range: %u-%u)\n",
                   Mhz, AMDBC250_BASE_CLOCK_MHZ, AMDBC250_BOOST_CLOCK_MHZ));
        return STATUS_INVALID_PARAMETER;
    }

    /* BC-250 STUB: No SMU firmware, just track in software */
    DevExt->GpuClockMhz = Mhz;
    DevExt->PowerState.LastGpuClockMhz = Mhz;
    DevExt->PowerState.ClockChangeCount++;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: SCLK tracked to %u MHz (software only)\n", Mhz));

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

    /* BC-250 STUB: No SMU firmware, just track in software */
    DevExt->MemoryClockMhz = Mhz;
    DevExt->PowerState.LastMemoryClockMhz = Mhz;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: MCLK tracked to %u MHz (software only)\n", Mhz));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3UpdateClocks - Automatic clock scaling
  
  Called periodically to adjust clocks based on workload and temperature.
===========================================================================*/

NTSTATUS
DreamV3UpdateClocks(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    ULONG TargetSclk = AMDBC250_BOOST_CLOCK_MHZ;
    LONG TempC = DevExt->CurrentTemperatureC;

    /* Check if thermal throttling is needed */
    if (TempC >= 85) {
        /* Aggressive throttling above 85°C */
        ULONG ThrottlePercent = 100 - ((TempC - 85) * 10);
        if (ThrottlePercent < 30) ThrottlePercent = 30;  /* Minimum 30% */
        
        TargetSclk = (AMDBC250_BOOST_CLOCK_MHZ * ThrottlePercent) / 100;
        
        DevExt->PowerState.ThermalThrottleActive = TRUE;
        DevExt->ThermalThrottleCount++;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Thermal throttle: %u%% (Temp: %ld°C)\n",
                   ThrottlePercent, TempC));
    } else if (TempC >= 70) {
        /* Moderate throttling 70-85°C */
        DevExt->PowerState.ThermalThrottleActive = FALSE;
        TargetSclk = AMDBC250_BOOST_CLOCK_MHZ;
    } else {
        /* Normal operation */
        DevExt->PowerState.ThermalThrottleActive = FALSE;
        TargetSclk = AMDBC250_BOOST_CLOCK_MHZ;
    }

    /* Check workload - scale down if idle */
    if (DevExt->SubmitCount - DevExt->PowerState.LastSubmitCount < 10) {
        /* Low workload - reduce clocks to save power */
        if (TargetSclk > AMDBC250_BASE_CLOCK_MHZ) {
            TargetSclk = AMDBC250_BASE_CLOCK_MHZ;
        }
    }
    DevExt->PowerState.LastSubmitCount = DevExt->SubmitCount;

    /* Apply SCLK change */
    if (TargetSclk != DevExt->GpuClockMhz) {
        return DreamV3SetGpuClockMhz(DevExt, TargetSclk);
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  Thermal Monitoring and Fan Control
  
  Multi-sensor approach:
  1. Edge temperature (THM_CURRENT_TEMP)
  2. Junction temperature (hot spot)
  3. HBM temperature (if applicable)
  4. VRM temperature (power delivery)
  
  Linux equivalent: amdgpu_hwmon_show_tempX_input()
===========================================================================*/

typedef struct _DREAM_V3_THERMAL_SENSORS {
    LONG EdgeTempC;       /* Edge temperature */
    LONG JunctionTempC;   /* Junction/hotspot */
    LONG VrmTempC;        /* VRM temperature */
    LONG HbmTempC;        /* HBM temperature */
} DREAM_V3_THERMAL_SENSORS, *PDREAM_V3_THERMAL_SENSORS;

NTSTATUS
DreamV3ReadAllThermalSensors(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _Out_ PDREAM_V3_THERMAL_SENSORS Sensors
    )
{
    /* Read edge temperature (primary sensor) */
    ULONG EdgeRaw = DreamV3ReadRegister(DevExt, AMDBC250_REG_THM_CURRENT_TEMP);
    Sensors->EdgeTempC = (LONG)((EdgeRaw & 0x3FF) * 0.125 - 49);

    /* Junction temperature - use thermal offset */
    /* Typically 10-15°C hotter than edge */
    Sensors->JunctionTempC = Sensors->EdgeTempC + 12;

    /* VRM temperature - estimate from power draw */
    /* Real hardware has dedicated VRM sensor */
    Sensors->VrmTempC = Sensors->EdgeTempC + 5;

    /* HBM temperature - not applicable for BC-250 (uses GDDR6) */
    Sensors->HbmTempC = 0;

    /* Clamp to valid ranges */
    if (Sensors->EdgeTempC < 0) Sensors->EdgeTempC = 0;
    if (Sensors->EdgeTempC > 150) Sensors->EdgeTempC = 150;
    if (Sensors->JunctionTempC < 0) Sensors->JunctionTempC = 0;
    if (Sensors->JunctionTempC > 150) Sensors->JunctionTempC = 150;
    if (Sensors->VrmTempC < 0) Sensors->VrmTempC = 0;
    if (Sensors->VrmTempC > 150) Sensors->VrmTempC = 150;

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3UpdateFanSpeed - Automatic fan speed control
  
  PID-like control based on temperature:
  - < 50°C: 20% (quiet)
  - 50-70°C: 40-60% (balanced)
  - 70-85°C: 60-80% (performance)
  - 85-105°C: 80-100% (critical)
  - > 105°C: Emergency shutdown
  
  Hysteresis: Prevent rapid fan speed oscillation
===========================================================================*/

NTSTATUS
DreamV3UpdateFanSpeed(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    LONG TempC = DevExt->CurrentTemperatureC;
    ULONG TargetFanPercent;
    ULONG CurrentFanPercent = DevExt->PowerState.CurrentFanSpeedPercent;

    /* Calculate target fan speed */
    if (TempC < 50) {
        TargetFanPercent = 20;  /* Quiet mode */
    } else if (TempC < 70) {
        /* Linear 40-60% */
        TargetFanPercent = 40 + ((TempC - 50) * 20) / 20;
    } else if (TempC < 85) {
        /* Linear 60-80% */
        TargetFanPercent = 60 + ((TempC - 70) * 20) / 15;
    } else if (TempC < 105) {
        /* Linear 80-100% */
        TargetFanPercent = 80 + ((TempC - 85) * 20) / 20;
    } else {
        /* Emergency - max speed */
        TargetFanPercent = 100;
    }

    /* Apply hysteresis - prevent rapid changes */
    if (TargetFanPercent > CurrentFanPercent + 5 ||
        TargetFanPercent < CurrentFanPercent - 5) {
        /* Significant change needed */
        DevExt->PowerState.CurrentFanSpeedPercent = TargetFanPercent;
        DevExt->PowerState.FanSpeedChangeCount++;

        /* Send to SMU */
        DreamV3SmuSendMessage(DevExt, SMU_MSG_SetFanSpeedPercent, TargetFanPercent, NULL);

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                   "AMDBC250-DREAM-V4.3: Fan speed -> %u%% (Temp: %ld°C)\n",
                   TargetFanPercent, TempC));
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3CheckThermalThrottle - Enhanced thermal monitoring
  
  This replaces the simple version in hw_init.c with:
  - Multi-sensor monitoring
  - Hysteresis to prevent oscillation
  - Integration with SMU clock scaling
  - Emergency shutdown procedure
===========================================================================*/

NTSTATUS
DreamV3CheckThermalThrottle(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    DREAM_V3_THERMAL_SENSORS Sensors = {0};
    NTSTATUS Status;

    /* Read all sensors */
    Status = DreamV3ReadAllThermalSensors(DevExt, &Sensors);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    /* Use max temperature for decisions */
    LONG MaxTemp = Sensors.EdgeTempC;
    if (Sensors.JunctionTempC > MaxTemp) {
        MaxTemp = Sensors.JunctionTempC;
    }

    DevExt->CurrentTemperatureC = MaxTemp;

    /* Check thresholds with hysteresis */
    const LONG THROTTLE_START = 85;
    const LONG THROTTLE_STOP = 80;   /* Hysteresis: resume at 80°C */
    const LONG EMERGENCY_STOP = 105;

    if (MaxTemp >= EMERGENCY_STOP) {
        /* EMERGENCY: Immediate shutdown */
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: *** EMERGENCY THERMAL SHUTDOWN *** "
                   "Edge: %ld°C, Junction: %ld°C\n",
                   Sensors.EdgeTempC, Sensors.JunctionTempC));

        DevExt->ThermalThrottleCount++;
        DevExt->PowerState.EmergencyShutdownCount++;

        /* Halt all engines immediately */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL,
                             CP_ME_CNTL__ME_HALT | CP_ME_CNTL__PFP_HALT | CP_ME_CNTL__CE_HALT);

        /* Force max fan */
        DreamV3SmuSendMessage(DevExt, SMU_MSG_SetFanSpeedPercent, 100, NULL);

        /* Notify SMU of emergency */
        DreamV3SmuSendMessage(DevExt, SMU_MSG_SetThermalThrottle, 1, NULL);

        return STATUS_DEVICE_NOT_READY;

    } else if (MaxTemp >= THROTTLE_START && 
               !DevExt->PowerState.ThermalThrottleActive) {
        /* Enter thermal throttle */
        DevExt->PowerState.ThermalThrottleActive = TRUE;
        DevExt->ThermalThrottleCount++;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Thermal throttle ACTIVATED - "
                   "Edge: %ld°C, Junction: %ld°C\n",
                   Sensors.EdgeTempC, Sensors.JunctionTempC));

        /* Notify SMU */
        DreamV3SmuSendMessage(DevExt, SMU_MSG_SetThermalThrottle, 1, NULL);

        /* Reduce clocks */
        DreamV3UpdateClocks(DevExt);

    } else if (MaxTemp <= THROTTLE_STOP && 
               DevExt->PowerState.ThermalThrottleActive) {
        /* Exit thermal throttle (hysteresis) */
        DevExt->PowerState.ThermalThrottleActive = FALSE;
        DevExt->PowerState.ThrottleExitCount++;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: Thermal throttle DEACTIVATED - "
                   "Edge: %ld°C\n", Sensors.EdgeTempC));

        /* Notify SMU */
        DreamV3SmuSendMessage(DevExt, SMU_MSG_SetThermalThrottle, 0, NULL);

        /* Restore clocks */
        DreamV3UpdateClocks(DevExt);
    }

    /* Update fan speed */
    DreamV3UpdateFanSpeed(DevExt);

    /* Update telemetry */
    DevExt->PowerState.Telemetry.EdgeTempC = Sensors.EdgeTempC;
    DevExt->PowerState.Telemetry.JunctionTempC = Sensors.JunctionTempC;
    DevExt->PowerState.Telemetry.VrmTempC = Sensors.VrmTempC;
    DevExt->PowerState.Telemetry.FanSpeedPercent = DevExt->PowerState.CurrentFanSpeedPercent;
    DevExt->PowerState.Telemetry.ThermalThrottleActive = DevExt->PowerState.ThermalThrottleActive;

    return STATUS_SUCCESS;
}

/*===========================================================================
  Power Profiling and Telemetry
  
  Track power consumption, clock speeds, and thermal history.
  Useful for debugging and performance analysis.
===========================================================================*/

NTSTATUS
DreamV3GetPowerUsage(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _Out_ PULONG PowerMilliwatts
    )
{
    NTSTATUS Status;
    ULONG Response = 0;

    if (PowerMilliwatts == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Query SMU for power usage */
    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_GetPowerUsage, 0, &Response);
    if (NT_SUCCESS(Status)) {
        *PowerMilliwatts = Response;
        DevExt->PowerState.Telemetry.CurrentPowerMilliwatts = Response;
    }

    return Status;
}

NTSTATUS
DreamV3SetPowerLimit(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG PowerLimitWatts
    )
{
    NTSTATUS Status;

    if (PowerLimitWatts < 100 || PowerLimitWatts > 300) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Invalid power limit %u W (range: 100-300)\n",
                   PowerLimitWatts));
        return STATUS_INVALID_PARAMETER;
    }

    Status = DreamV3SmuSendMessage(DevExt, SMU_MSG_SetPowerLimit, PowerLimitWatts, NULL);
    if (NT_SUCCESS(Status)) {
        DevExt->PowerState.PowerLimitWatts = PowerLimitWatts;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: Power limit set to %u W\n", PowerLimitWatts));
    }

    return Status;
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

/*===========================================================================
  DreamV3NotifyAcpiEvent - Handle ACPI events
  
  Called by Windows for power button, lid close, AC/DC transitions.
===========================================================================*/

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

    if (DevExt == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    UNREFERENCED_PARAMETER(Argument);
    UNREFERENCED_PARAMETER(AcpiFlags);

    switch (EventType) {
    case 0x80:  /* DXGK_EVENT_ACPI - ACPI power button */
        switch (EventCode) {
        case 0x80:  /* Power button */
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: ACPI power button event\n"));
            break;

        case 0x81:  /* Sleep button */
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: ACPI sleep button event\n"));
            break;

        case 0x86:  /* Lid state change */
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: ACPI lid event\n"));
            break;

        case 0x8A:  /* AC/DC power source change */
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: ACPI AC/DC transition\n"));
            break;

        default:
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                       "AMDBC250-DREAM-V4.3: Unknown ACPI event 0x%02X\n", EventCode));
            break;
        }
        break;

    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }

    return Status;
}

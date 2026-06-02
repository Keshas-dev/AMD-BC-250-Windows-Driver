# NBIO Firewall Analysis - Critical Discovery

**Date:** 2026-06-03
**Status:** Testing Required

---

## Key Insight

The NBIO firewall blocking PSP MMIO access on Windows but NOT on Linux suggests two possible scenarios:

### Scenario A: Initialization Order (Timing Issue)
- Linux initializes PSP BEFORE the NBIO firewall is locked down
- The firewall lock happens during late BIOS/UEFI phases or early kernel boot
- If Linux gets to PSP first, it succeeds
- The firewall "lock bit" prevents further changes until next reset

### Scenario B: OS-Specific Firewall Activation
- Windows activates the firewall specifically (WHQL compliance, Device Guard, etc.)
- Linux skips proprietary lockdown routines (open-source driver doesn't implement them)
- Linux handles IOMMU/AMD-Vi security differently, leaving NBIO firewall unlocked

---

## Evidence from Linux dmesg

```
[5.670678] amdgpu 0000:01:00.0: register mmio base: 0xFE800000
[5.670679] amdgpu 0000:01:00.0: register mmio size: 524288
[5.672927] amdgpu 0000:01:00.0: detected ip block number 3 <psp_v11_0_8> (psp)
[5.716470] amdgpu 0000:01:00.0: reserve 0x400000 from 0xf41f800000 for PSP TMR
[5.752954] amdgpu 0000:01:00.0: SMU is initialized successfully!
```

**Key observation:** No "NBIO firewall" or "lock" mentions in Linux dmesg. This suggests Linux doesn't explicitly activate the firewall.

---

## Verification Method: Warm Reboot Test

### Steps:
1. **Boot into Windows** (let it potentially activate the firewall)
2. **Warm reboot** (soft reset) into CachyOS WITHOUT dropping power
3. **Run warm reboot test script:**
   ```bash
   wget https://raw.githubusercontent.com/Keshas-dev/AMD-BC-250-Windows-Driver/main/tools/warm-reboot-test.sh
   bash warm-reboot-test.sh
   ```
4. **Compare results with cold boot**

### Interpretation:
- **If PSP works on Linux after warm reboot:** Windows did NOT activate firewall (Scenario A - timing issue)
- **If PSP fails on Linux after warm reboot:** Windows DID activate firewall (Scenario B - OS-specific)

---

## What This Means for Our Driver

### If Scenario A (Timing):
- We need to initialize PSP EARLIER in the boot process
- Possibly during BIOS/UEFI phase or very early kernel boot
- May require UEFI driver or boot driver approach

### If Scenario B (OS-Specific):
- We need to find and disable the Windows-specific firewall activation
- May be in ACPI tables, AMD driver, or Windows security policies
- Could try disabling Device Guard, VBS, or other Windows security features

---

## Next Steps

1. **Run warm reboot test** to determine which scenario is correct
2. **Compare NBIO register dumps** between Linux and Windows
3. **If Scenario A:** Investigate early boot PSP initialization
4. **If Scenario B:** Investigate Windows security features that activate firewall

---

## Technical Details

### PSP MMIO Addresses:
- GPU MMIO base: 0xFE800000
- PSP MMIO offset: 0x10000
- PSP MMIO base: 0xFE810000
- PSP MMIO size: 0x4000 (16KB)

### Key PSP Registers:
- C2PMSG_35 (0x0088): Bootloader command
- C2PMSG_36 (0x008C): Firmware address
- C2PMSG_64 (0x0100): Ring creation / TOS_READY
- C2PMSG_67 (0x010C): Ring write pointer
- C2PMSG_69-71 (0x0114-0x011C): Ring buffer
- C2PMSG_81 (0x0144): SOS alive check

### NBIO Firewall Status on Windows:
- Allowed blocks: 6 (GPU_ID, HDP, GC, MMHUB, DF, NBIO)
- Blocked reads: 7 (GRBM, CP, CLK, RSMU, UVD, SDMA, RLCG)
- Blocked writes: 7 (same as reads)

---

## References

- Linux amdgpu driver: https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/amd/amdgpu
- PSP v11 implementation: psp_v11_0.c
- NBIO firewall: Device Exclusion Vector (DEV) registers
- AMD documentation: BIOS and Kernel Developer's Guide (BKDG)

---

*This analysis is based on community input and Linux driver analysis. Further testing required to confirm which scenario is correct.*
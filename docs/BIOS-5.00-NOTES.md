# BIOS 5.00 Notes — BC-250 (Cyan Skillfish)

**BIOS File:** BC250_5.00_clv.bin  
**BIOS Date:** Unknown (5.00 revision)  
**Date:** 2026-06-12  

---

## BIOS Version History

| Version | Notes |
|---------|-------|
| P3.00 | 512MB VRAM, older PSP firmware |
| P4.00G | American Megatrends 2022-04-08, 8GB VRAM |
| **P5.00** | **Current — latest known, updated by user** |

The BIOS content for BC250_5.00_clv.bin is extracted via RU.efi and contains:
- ATOM BIOS: 113-AMDRBN-003
- Video BIOS tables (VBT)
- $PSP table pointing to embedded SOS firmware

## Impact of BIOS 5.00 on Register Layout

BIOS 5.00 continues to use the same register layout as P4.00G:
- GC_BASE = 0x1260 (confirmed via register reads)
- THM_BASE = 0x8000 (confirmed, NOT 0x16600 from Linux headers)
- MP1_BASE = 0x16000 (SMU registers at these offsets)

## PSP Firmware State with BIOS 5.00

**SOS pre-loaded by BIOS**: C2PMSG_81 = 0xF0000010 at power-on.
- SOS firmware is embedded in the BIOS $PSP table
- BIOS loads SOS before handing control to the OS
- Our PSP driver's BOOT_SEQUENCE is redundant

## VRAM Configuration

Tested on this unit (BIOS 5.00):
- 512MB VRAM reported (same as P3.00 test unit)
- VRAM aperture visible at PCI BAR0 (256MB)
- GPU driver reports 16384 MB via GET_VRAM_INFO (hardcoded or from ACPI)

## THM Register Behavior

Confirmed working on BIOS 5.00:
- THM_CTRL [0x8000] = 0x18 → writable (0x800000EF accepted)
- THM_CURRENT_TEMP [0x8008] = read-only, returns temperature
- **Linux offset 0x16600 is WRONG on this BIOS**

## SMU Status

SMU firmware does NOT respond on BIOS 5.00:
- C2PMSG_66/82/90 at Navi10 offsets (0x16104+) — returns 0
- C2PMSG_66/82/90 at BC-250 offsets (0x16A08+) — returns 0
- TestMessage (0x1), GetSmuVersion (0x2) — no response
- Writes to message registers silently ignored

Possible reasons:
1. SMU is power-gated (GC block idle → SMU off)
2. SMU firmware not loaded (needs PSP to load)
3. SMU clock gated (needs RSMU to enable)
4. SMU registers at different offsets on this BIOS revision

## NBIO Firewall with BIOS 5.00

Same behavior as P4.00G:
- 0xC000-0xCFFF range: writes blocked from ALL paths
- GC_BASE-shifted aliases (0xDA60+): bypass NBIO
- Some registers still read-only by hardware design

## Register Read Values (BIOS 5.00)

| Register | Offset | Value |
|----------|--------|-------|
| GPU_ID | 0x0000 | 0x9FFF9700 |
| GRBM_STATUS | 0x3260 | 0x00000000 |
| CC_GC_SHADER_ARRAY_CONFIG | 0x3264 | 0x00000000 |
| SPI_PG_ENABLE_STATIC_WGP_MASK | 0x34FC | 0x00002000 |
| CP_SCRATCH | 0x32D4 | 0x4D585042 |
| CP_RING0_BASE_LO | 0xDA60 | 0x00000000 |
| CP_RING0_CNTL | 0xDA68 | 0x00000000 |
| CP_RING0_RPTR | 0xDA6C | 0x01200000 |
| CP_RING0_WPTR | 0xDA78 | 0x00100010 |
| KIQ_BASE_LO | 0xE060 | 0x00000000 |
| KIQ_CNTL | 0xE068 | 0x00000000 |
| THM_CTRL | 0x8000 | 0x00000018 |
| NBIO_ID | 0xC100 | 0xFEDCBAEF |

## BIOS Update via RU.efi

If BIOS reflash is needed:
1. Boot UEFI shell (from USB or boot manager)
2. Run `RU.efi` from USB
3. Navigate to SPI flash region
4. Write BC250_5.00_clv.bin to appropriate offset

**WARNING**: RU.efi can brick the card if used incorrectly. Only use if absolutely necessary.

## References

- `GC_BASE-REGISTER-ANALYSIS.md` — detailed register analysis
- `REGISTER-MAP-BC250.md` — complete register map
- `docs/UEFI-TOOLS-GUIDE.md` — RU.efi usage guide
- `docs/BIOS-SETTINGS.md` — BIOS configuration options

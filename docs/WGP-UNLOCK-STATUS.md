# WGP Unlock Status — 2026-09-15

## DEFINITIVE: WGP unlock is BLOCKED on our hardware

### What was tried

| Method | Result | Date |
|--------|--------|------|
| Direct BAR5 SPI_PG write | LOCKED (readback 0x00000000) | 2026-08-06 |
| Per-bank GRBM select + write | LOCKED (SPI_PG=0 all banks) | 2026-08-01 |
| SMU Q3 msg 0x98 (ungated SMN write) | No known SPI_PG alias found | 2026-08-06 |
| SMN GC alias scan (0x01100000-0x01200000) | NO aliases found | 2026-08-01 |
| SMN GC alias scan (0x03B10000-0x03B11000) | NO aliases found | 2026-08-01 |
| SMU RequestActiveWgp (Q0 0x18) | Accepted but WGP=0 | 2026-08-06 |
| UNLOCK_40CU IOCTL (0x80000980) | LOCKED | 2026-08-06 |
| KMD Step 0c (CC=0, SPI=0x1F, RLC=0x1F) | LOCKED | 2026-08-09 |
| EFI Shell WGP_unlock.nsh | **FAILED — NBIO LOCKED at boot** | 2026-08-11 |
| VBIOS SMU wake sequence | NOT IMPLEMENTED | N/A |

### Why EFI Shell WGP unlock doesn't work for us

From our EFI Shell boot test logs (2026-08-11):
- **NBIO is LOCKED at EFI boot** — C2PMSG registers show 0xFF
- User enabled "IOMMU Disabled" in BIOS — still locked
- `mm` commands to SPI_PG return 0 (locked) even at EFI phase
- No BIOS option to unlock NBIO pre-boot on this board

### What WORKS (confirmed)

| Function | Method | Status |
|----------|--------|--------|
| Display (2560x1440) | KMDOD driver | ✅ OK |
| SMU freq/voltage | SMU Q0/Q3 | ✅ OK (1500MHz @ 931mV) |
| SMU features | SMU Q2/Q3 | ✅ OK (GFXOFF/CG/PG toggle) |
| CPU core unlock | SMU Q3 0x98 → SMN 0x0115A870 | ✅ OK (6c→8c, volatile) |
| Temperature | SMN 0x03B10000+ | ✅ OK (edge, junction, mem) |
| VRAM info | KMD GET_VRAM_INFO | ✅ OK (16GB after fix) |
| Vulkan ICD | bc250_icd_stub.dll | ✅ OK (full pipeline, no 3D) |
| SMU version | SMU Q0 0x02 | ✅ OK (v88.6.0) |
| CMOS VRAM config | Ports 0x72/0x73 | ✅ OK |

### What DOESN'T work (all SOS/software locked)

| Function | Blocker | Fixable? |
|----------|---------|----------|
| WGP/SPI_PG | SOS-locked from host | ❌ Not on Windows (EFI/BIOS locked too) |
| Ring BASE | SOS-locked | ❌ Not on Windows |
| 3D/shader execution | WGP=0 (consequence) | ❌ Until WGP unlock |
| VCN dom6 power | SMU Q3 0x3C insufficient | ❌ (fabric open but not powered) |
| GCVM PT_BASE | HW-locked | ❌ |

### Conclusion

**3D graphics on BC-250 Windows is IMPOSSIBLE without WGP unlock.**
WGP unlock is BLOCKED on our hardware (BIOS NBIO locked, Windows SOS-locked, EFI tested and failed).

The card is usable for: display + SMU control + monitoring + CPU OC + Vulkan ICD stub (no 3D).

### Alternative paths (not yet explored on our hardware)

1. **Linux boot** — amdgpu kernel has debugfs privilege, can write SPI_PG before SOS locks it
2. **BIOS mod** — unlock NBIO in BIOS (unconfirmed if possible on BC-250)
3. **WDDM miniport** — full init order like Linux (GART+PSP ring before SPI_PG write)
4. **Firmware modification** — modify SMU firmware to ignore SOS lock

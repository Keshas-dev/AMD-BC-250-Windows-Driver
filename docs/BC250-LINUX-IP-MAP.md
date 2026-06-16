## BC-250 Cyan Skillfish — Linux IP Base Address Map

Source: Linux kernel `cyan_skillfish_ip_offset.h` (torvalds/linux)

### Architecture
- Codename: Cyan Skillfish
- Architecture: GFX1013 (RDNA 1.5)
- PCI Device ID: 0x13FE
- IP Versions (from `amdgpu_discovery.c`):
  - GC: 10.1.3
  - NBIO: 2.1.1
  - MMHUB: 2.0.3
  - ATHUB: 2.0.3
  - OSSSYS: 5.0.1
  - HDP: 5.0.1
  - SDMA0: 5.0.1
  - SDMA1: 5.0.1
  - DF: 3.5.0
  - UMC: 8.1.1
  - MP0 (PSP): 11.0.8
  - MP1: 11.0.8
  - THM: 11.0.1
  - SMUIO: 11.0.8
  - UVD: 2.0.3

### IP Base Addresses (BAR5 MMIO offsets)

Linux uses `SOC15_REG_OFFSET(IP, inst, reg)` = `IP_BASE.instance[inst].segment[0] + reg_offset`

| IP Block | Base Address | Segment 1 | Notes |
|----------|-------------|-----------|-------|
| GC | 0x1260 | 0xA000 | Graphics Command Processor |
| NBIO | 0x0000 | 0x14, 0xD20, 0x10400 | PCI-E Bridge |
| HDP | 0x0F20 | - | Host Data Port (memory access) |
| ATHUB | 0x0C00 | - | Address Translation Hub |
| MMHUB | 0x1A000 | - | Memory Management Hub |
| DF | 0x7000 | - | Data Fabric (fabric train) |
| OSSSYS | 0x10A0 | - | OS Support (doorbell, IH) |
| MP0 | 0x16000 | - | PSP Processor 0 |
| MP1 | 0x16000 | - | PSP Processor 1 |
| THM | 0x16600 | - | Thermal Management |
| SMUIO | 0x16800 | 0x16A00 | SMO I/O |
| CLK | 0x16C00 | +0x200 step (6 instances) | Clock Management |
| UMC0 | 0x14000 | - | Unified Memory Controller |
| FUSE | 0x17400 | - | Fuse/Speed Info |
| UVD0 | 0x7800 | 0x7E00 | Unified Video Decoder |
| DMU | 0x12, 0xC0, 0x34C0, 0x9000 | - | Display Management Unit |

### How Linux Calculates Final Register Offsets

For a GC register with Navi10 offset `reg`:
```
BAR5_offset = GC_BASE__INST0_SEG0 + reg = 0x1260 + reg
```

Examples:
| Register | Navi10 Offset | + GC_BASE | Final BAR5 Offset |
|----------|--------------|-----------|-------------------|
| GRBM_STATUS | 0x2000 | + 0x1260 | **0x3260** ✅ |
| GRBM_STATUS_SE0 | 0x2000 | + 0x1260 | **0x3260** |
| CC_GC_SHADER_ARRAY_CONFIG | 0x2004 | + 0x1260 | **0x3264** |
| GRBM_CNTL | 0x200C | + 0x1260 | **0x326C** |
| SCRATCH_REG | 0x2074 | + 0x1260 | **0x32D4** |
| SPI_PG_ENABLE_STATIC_WGP_MASK | 0x229C | + 0x1260 | **0x34FC** |

For NBIO registers (NBIO_BASE = 0x0000):
```
BAR5_offset = NBIO_BASE + reg = 0x0000 + reg = reg
```
NBIO registers are at their raw offsets (0xC000+ range).

For MMHUB (base = 0x1A000):
```
BAR5_offset = MMHUB_BASE + reg = 0x1A000 + reg
```

For THM (base = 0x16600):
```
BAR5_offset = THM_BASE + reg = 0x16600 + reg
```

### Key Registers for GPU Init (from Linux gfx_v10_0.c)

1. **GC_GRBM_CNTL** (0x326C): GRBM select — write to select SE/SH/SX
2. **CC_GC_SHADER_ARRAY_CONFIG** (0x3264): CU enumeration mask
   - Stock: 0xFFF80000 (24 CU)
   - 40CU unlock: 0xFFE00000 (40 CU)
3. **SPI_PG_ENABLE_STATIC_WGP_MASK** (0x34FC): WGP dispatch mask
   - Stock: 0x7 (3 WGP = 6 CU per SA)
   - 40CU unlock: 0x1F (5 WGP = 10 CU per SA)
4. **GRBM_STATUS** (0x3260): GPU busy/idle status

### Linux Init Order (from gfx_v10_0.c → nv.c → amdgpu_device.c)

1. `cyan_skillfish_reg_base_init()` — set IP base offsets
2. `amdgpu_discovery_set_ip_blocks()` — set IP versions
3. `nv_common_early_init()` — set cg/pg flags, external_rev_id
4. `nv_reg_base_init()` — register base addresses
5. GPU init sequence (GFX, GMC, SDMA, etc.)
6. Firmware loading via PSP (psp_v11_0_8)

### Important Notes

- `cg_flags = 0` and `pg_flags = 0` for Cyan Skillfish (no clock gating / power gating)
- `external_rev_id = rev_id + 0x82`
- `xcc_mask = 1` (single GFX engine)
- `sdma.num_instances = 2`
- Linux skips PSP firmware loading: `psp_v11_0_8` is minimal — no ring protocol support
- `gpu_info` firmware required for IP discovery variant (cyan_skillfish_gpu_info.bin)

### Windows Test Results (INIT_HARDWARE NBIO_MAP, 2026-06-16)

Test: `quick-init.exe` — INIT_HARDWARE NBIO_MAP + READ_REG
Driver fix: moved GPU alive test after NBIO_MAP break (no register read during init)

| Offset | Register | Value | Status | Notes |
|--------|----------|-------|--------|-------|
| 0x00000 | GPU_ID | 0x9FFF9700 | ✅ | Confirms BAR5 MMIO working |
| 0x03260 | GRBM_STATUS | 0x00000000 | ✅ | GPU idle, no active shaders |
| 0x03264 | CC_GC_SHADER_ARRAY_CONFIG | 0x00000000 | ✅ | Read OK |
| 0x032D4 | SCRATCH_REG | 0x4D585042 | ✅ | "BMXP" — proves read works |
| 0x034FC | SPI_PG_ENABLE_STATIC_WGP_MASK | 0x00002000 | ✅ | Read OK |
| 0x00F20 | HDP | 0xFFFFFFFF | ❌ | NBIO blocks or unmapped |
| 0x16600 | THM | 0x00000000 | ❌ | Not accessible |
| 0x16800 | SMUIO | 0x00000000 | ❌ | Not accessible |
| 0x0C060 | CP_ME_CNTL | 0x00000000 | ❌ | NBIO blocks |

**Conclusion**: GC_BASE-shifted registers (0x3200+) are fully accessible via BAR5 MMIO. Other IP blocks (HDP, THM, SMUIO, CP) are NOT accessible — NBIO blocks or addresses not mapped in BAR5 space.

### PM4/CP Probe Results (2026-06-16)

Test: `pm4-probe.exe` — INIT_HARDWARE NBIO_MAP + register scan + SEND_PM4

**CP Registers (0xC000+ native range):**
| Offset | Register | Value | Status |
|--------|----------|-------|--------|
| 0xC0D8 | NBIO_CTRL | 0x00001020 | ✅ Read OK |
| 0xC100 | NBIO_ID | 0xFEDCBAEF | ✅ Read OK |
| 0xC104 | NBIO_CFG | 0x00A00000 | ✅ Read OK |
| 0xC060 | CP_ME_CNTL | 0x00000000 | ⚠️ Reads 0 (may be blocked or zero) |
| 0xC064 | CP_ME_STATUS | 0x00000000 | ⚠️ Reads 0 |
| 0xC0E0 | CP_MEC_CNTL | 0x00000000 | ⚠️ Reads 0 |
| 0xC0E4 | CP_MEC_STATUS | 0x00000000 | ⚠️ Reads 0 |

**CP Registers via GC_BASE shifted (0x3200+):**
| Offset | Value | Status |
|--------|-------|--------|
| 0x3AD8 | 0x02A80940 | ✅ Fence/doorbell related |
| 0x3ADC | 0x02A829A0 | ✅ |
| 0x3AE0 | 0x02A82940 | ✅ |
| 0x3AE4 | 0x02A82940 | ✅ |
| 0x3AE8 | 0x02A82940 | ✅ |
| 0x3AEC | 0x02A82940 | ✅ |

**SCRATCH Register Write Behavior:**
- SCRATCH (0x32D4) bit 31 is write-masked: writing 0xDEADBEEF reads back 0x5EADBEEF
- Writing 0xCAFEBABE reads back 0x4AFEBABE
- Writing 0x12345678 reads back 0x12345678 (bit 31 already 0)
- **Conclusion**: SCRATCH bit 31 is hardware-controlled (read-only or W1C)

**SEND_PM4:**
- NOP: FAIL (err=21 = STATUS_DEVICE_NOT_READY) — rings not initialized
- NOP+EOP: FAIL (err=21) — same reason
- SEND_PM4 requires `GfxRing.VirtualAddress != NULL` which is only set during full GPU init

### 40 CU Unlock (from duggasco/bc250-40cu-unlock)

Two registers control CU availability:
- `CC_GC_SHADER_ARRAY_CONFIG` (0x3264): Enumeration mask
- `SPI_PG_ENABLE_STATIC_WGP_MASK` (0x34FC): Dispatch gate
Both must be written together. Neither alone produces compute scaling.

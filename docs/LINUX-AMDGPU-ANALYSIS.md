# Linux amdgpu PSP v11.0_8 Driver Analysis — BC-250 (Cyan Skillfish2)

## Source Files (Linux kernel 6.12+)

- `drivers/gpu/drm/amd/amdgpu/psp_v11_0_8.c` — Dedicated PSP driver for CYAN_SKILLFISH2
- `drivers/gpu/drm/amd/amdgpu/psp_v11_0_8.h`
- `drivers/gpu/drm/amd/amdgpu/psp_v11_0.c` — Common Navi10 PSP functions
- `drivers/gpu/drm/amd/include/asic_reg/mp/mp_11_0_8_offset.h` — Register offsets for CYAN_SKILLFISH2
- `drivers/gpu/drm/amd/amdgpu/soc15.h` — SOC15 register access macros (RREG32_SOC15/WREG32_SOC15)
- `drivers/gpu/drm/amd/amdgpu/nbio_v7_4.c` — NBIO registers (PSB/PSP access control)

## PCI Topology

```
01:00.0  Display controller [0300]: Advanced Micro Devices, Inc. [AMD] Device 13FE (rev c1)
    Subsystem: Device 13FE:13FE
    Region 0: Memory at fce0000000 (64-bit, prefetchable) [size=32M]
    Region 2: Memory at e000000000 (64-bit, prefetchable) [size=16G]
    Region 4: Memory at fc00000000 (64-bit, prefetchable) [size=8M]
    Region 5: Memory at fe800000 (64-bit, prefetchable) [size=512K]  ← GPU BAR5 (PSP MMIO!)
    Capabilities: [100 v2] Vendor Specific Information: AMD Container Header

01:00.1  Audio device [0403]: Advanced Micro Devices, Inc. [AMD] Device 13F6
01:00.2  Encryption controller [1080]: Advanced Micro Devices, Inc. [AMD] Device 143E  ← PSP device
```

**KEY FINDING**: GPU function 0 at `01:00.0` has **BAR 5 at 0xFE800000 (512KB)**. Linux accesses PSP registers **through this BAR**, NOT through function 2 (1022:143E). The NBIO firewall checks PCI requester ID — function 0 (01:00.0) is trusted, function 2 (01:00.2) is blocked for PSP MMIO.

## PSP Initialization Flow

### 1. PSP VTable Structure (`psp_v11_0_8_funcs` in `psp_v11_0_8.c`)

```c
const struct psp_funcs psp_v11_0_8_funcs = {
    .init_microcode        = NULL,                 /* NOT USED — firmware pre-loaded by BIOS */
    .bootloader_load_sysdrv = NULL,                /* NOT USED — firmware pre-loaded by BIOS */
    .bootloader_load_sos   = NULL,                 /* NOT USED — firmware pre-loaded by BIOS */
    .ring_create           = psp_v11_0_ring_create, /* From psp_v11_0.c (Navi10 compatible) */
    .ring_stop             = psp_v11_0_ring_stop,   /* From psp_v11_0.c */
    .ring_destroy          = psp_v11_0_ring_destroy,/* From psp_v11_0.c */
    .ring_wptr_update      = NULL,                 /* Simple register write */
    .cmd_submit            = NULL,                 /* Simple ring write + wptr update */
};
```

**All NULL function pointers** → the calling code in `psp_init.c` has:
```c
if (psp->funcs->init_microcode)
    return psp->funcs->init_microcode(psp);
```
Since the pointers are NULL, these steps are **skipped entirely**.

### 2. MP0 Base Address Discovery

MP0 base is **NOT hardcoded**. It comes from the **IP Discovery table** in VBIOS:

```c
/* In psp_v11_0_8_init():
 * 1. Read IP Discovery table from VBIOS (via amdgpu_discovery)
 * 2. Find MP0 IP entry (harvest_id != 0 means disabled)
 * 3. Get base address from IP discovery entry
 */
mp0_base = amdgpu_discovery_get_ip_base(adev, MP0_HWIP);
```

The IP Discovery table is a structured table embedded in the VBIOS ROM that lists all hardware IP blocks and their register base offsets. Linux reads it during early init.

### 3. Register Access Mechanism

```c
/* soc15.h: RREG32_SOC15 macro */
#define RREG32_SOC15(ip, inst, reg)                          \
    adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] +      \
    reg##_BASE_IDX * reg offset

/* Physical address calculation:
 * 1. reg_offset[MP0_HWIP][0] = DWORD offset from IP Discovery (e.g., 0x14000)
 * 2. reg = register offset in DWORDs (e.g., mmMP0_SMN_C2PMSG_35 = 0x0063)
 * 3. Base index (usually 2 for SOC15 registers)
 *
 * Physical = GPU_BAR_base + (discovery_dword + reg_dword) * 4
 *          = 0xFE800000   + (0x14000 + 0x0063) * 4
 *          = 0xFE800000   + 0x5018C
 */
```

### 4. Register Offsets (`mp_11_0_8_offset.h`)

| Register | DWORD Offset | Byte Offset | Description |
|----------|-------------|-------------|-------------|
| `mmMP0_SMN_C2PMSG_35` | 0x0063 | 0x018C | Bootloader command (load sysdrv/sos) |
| `mmMP0_SMN_C2PMSG_36` | 0x0064 | 0x0190 | Firmware physical address (>> 20) |
| `mmMP0_SMN_C2PMSG_64` | 0x0080 | 0x0200 | TOS mailbox (ready/response/command) |
| `mmMP0_SMN_C2PMSG_67` | 0x0083 | 0x020C | Ring write pointer |
| `mmMP0_SMN_C2PMSG_69` | 0x0085 | 0x0214 | Ring base address low |
| `mmMP0_SMN_C2PMSG_70` | 0x0086 | 0x0218 | Ring base address high |
| `mmMP0_SMN_C2PMSG_71` | 0x0087 | 0x021C | Ring size |
| `mmMP0_SMN_C2PMSG_81` | 0x0091 | 0x0244 | Sign of Life (SOS alive indicator) |
| `mmMP0_SMN_C2PMSG_101` | 0x00A5 | 0x0294 | (Used for various commands) |

**These offsets are IDENTICAL to Navi10 (mp_11_0_offset.h).** BC-250 uses the same mailbox register layout.

### 5. Ring Communication Protocol

1. **Wait for TOS ready**: Poll `C2PMSG_64` until `(value & 0x80000000) == 0x80000000`
2. **Program ring**: Write ring address (low/high) to `C2PMSG_69/70`, ring size to `C2PMSG_71`
3. **Send command**: Write 0 to `C2PMSG_64` (triggers PSP to read ring params)
4. **Wait for response**: Poll `C2PMSG_64` until `(value & 0x80000000) == 0x80000000`
5. **Submit command**: Write command to ring buffer, update `C2PMSG_67` with new write pointer
6. **Ring commands** (GFX_CTRL_CMD_ID):
   - `0x00020000` — Destroy rings / NBIO unlock request

### 6. NBIO Firewall Bypass (Linux Method)

Linux NBIO driver (`nbio_v7_4.c`) handles BC-250's NBIO registers:

```c
/* BIF_BX_PF0_BIF_CFG_DEVICE_CNTL (offset 0xC100) — PCI config access control */
/* BIF_BX_PF0_SYSHUB_BIF_CFG_BIF_MMHUB_ACCESS_CNTL (offset 0x50D0) — MMHUB access control */
/* BIF_BX_PF0_SYSHUB_BIF_CFG_BIF_PSB_ACCESS_CNTL (offset 0xC180) — PSB/PSP access control */
```

The NBIO unlock sequence writes specific signatures to these registers to disable the firewall:

1. Write `0xFEDCBAEF` to NBIO register at MMIO offset `0xC100` (relative to MP0 base)
2. Write `0xFEDCBADF` to NBIO register at MMIO offset `0xC180` (relative to MP0 base)
3. Read NBIO register at MMIO offset `0x50D0` to verify unlock

**IMPORTANT**: These writes go through **GPU BAR5**, same as all other PSP/NBIO registers. The NBIO firewall on BC-250 blocks accesses from external PCIe devices or untrusted functions, but function 0's BAR accesses are authorized.

### 7. SOS Alive Detection

Before attempting any PSP initialization, Linux checks if SOS (Secure OS) is already running:

```c
/* Read Sign of Life register */
sol = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81);
if (sol != 0) {
    /* SOS already alive — skip firmware loading */
    psp->sos_alive = true;
}
```

On BC-256, the BIOS pre-loads the PSP firmware and starts the Secure OS, so C2PMSG_81 will read non-zero on a cold boot. The Linux driver skips all firmware loading and goes straight to ring setup.

## Linux Boot Logs (CachyOS 7.0.10)

```
amdgpu: ATOM BIOS: 113-D7010500-001
amdgpu: PSP firmware loading is not needed for CYAN_SKILLFISH2
amdgpu: psp_v11_0_8: no sos firmware? trying fallback
amdgpu: Found VCN firmware binary field expected_size: 0
amdgpu: psp gfx fw loading...
amdgpu: psp gfx fw loading-2...
amdgpu: [PSP] ring 0 creates successfully
amdgpu: [PSP] ring 1 creates successfully
amdgpu: tmr_size: 4MB
amdgpu: PSP TMR (4MB) allocated at 0x00000000F41F8000
amdgpu: psp_autoload_init succeeded.
amdgpu: REGISTER_REGS success
amdgpu: psp mode is autoload, skip psp reload
amdgpu: autoload in psp
amdgpu: RAP: optional rap ta ucode isn't needed
amdgpu: SEC: optional sec ta ucode isn't needed
amdgpu: [PSP] ring 4 creates successfully
amdgpu: SMU is already running, skipping SMU firmware loading...
amdgpu: SMU: driver doesn't support any message for CYAN_SKILLFISH2
amdgpu: SMU: driver doesn't support any message for CYAN_SKILLFISH2
amdgpu: REGISTER_WITH_RLC success
```

Key observations from logs:
- **"PSP firmware loading is not needed for CYAN_SKILLFISH2"** — BIOS pre-loads firmware
- **"no sos firmware? trying fallback"** — SOS firmware blob missing from filesystem (normal)
- **"ring 0/1 creates successfully"** — Only ring setup needed
- **"TMR (4MB) allocated at 0xF41F8000"** — Trusted Memory Region allocated by PSP
- **"SMU is already running"** — SMU firmware also pre-loaded by BIOS
- **"driver doesn't support any message for CYAN_SKILLFISH2"** — SMU communication not needed

## Key Takeaways for Windows Driver

1. **Access PSP through GPU BAR5 (0xFE800000)** — NOT through function 2 (encryption controller)
2. **PSP firmware is pre-loaded by BIOS** — No firmware loading needed (SOS is already alive)
3. **Only ring setup is needed** — Create rings, send unlock command, done
4. **MP0 base must be discovered** — Use IP discovery table or scan for SOS alive register
5. **Register offsets match Navi10** — Reuse Navi10 register definitions
6. **SMU is also pre-loaded** — No SMU firmware loading needed
7. **NBIO unlock sends signatures to 0xC100/0xC180** — Uses PSP ring to bypass firewall

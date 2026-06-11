# AMD BC-250 Windows Driver — Agent Notes

## Architecture
- **Two-driver hybrid model**: GPU driver (`atikmdag.sys`) + PSP driver (`PspDriver.sys`) run in parallel.
- GPU driver: `\\.\AMDBC250DreamV43`, WDM control device + IOCTL channel
- PSP driver: `\\.\AmdBcPsp`, separate kernel driver for PSP firmware/ring control
- Not real WDDM — `DxgkInitialize` never called (not exported on Win11 26100+)
- Coexists with Microsoft BasicDisplay (handles actual display output)

## PSP Driver Repo
`C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\`
- Kernel: `src\driver\PspDriver.c` (~1305 lines)
- IOCTL defines: `inc\PspIoctl.h`
- Test tool: `src\test\test-psp-driver.c` (~813 lines)
- Embedded FW: `inc\firmware_data.h` (32KB+, BIOS 5.00)

## Build
- **GPU driver**: `build.bat` (VS2022 + WDK 10.0.26100.0 on E: or C:)
- **PSP driver**: `scripts\build.bat` from PSP driver repo
- Run as Administrator (cert trust to LocalMachine)
- Test-signing: `bcdedit /set testsigning on` + reboot, Secure Boot OFF
- Certificate: `CN=AMD-BC250-Signer` in LocalMachine\Root + LocalMachine\TrustedPublisher

## Install (ALWAYS uninstall first + reboot)
1. Device Manager → Uninstall (check "Delete driver") → Reboot
2. Device Manager → Update Driver → Browse to `output\` → Reboot
3. Order: PSP driver first (device `PCI\VEN_1022&DEV_143E`), then GPU driver

## Known Critical Bugs (PSP Driver)

### Bug 1: C2PMSG_81 save/restore in PspSendMailboxCommand (`PspDriver.c:124-197`)
- Saves `C2PMSG_81` before command, **restores it after** — clobbers PSP response.
- Correct Linux protocol: poll `C2PMSG_35` until it clears (not C2PMSG_81).
- **Fix**: Remove save/restore; poll C2PMSG_35 for completion.

### Bug 2: Spinlock held during polling (`PspDriver.c:117-200`)
- `KeAcquireSpinLock` then `KeStallExecutionProcessor(1000)` up to 500ms.
- All other threads blocked at DISPATCH_LEVEL during entire wait.
- **Fix**: Release spinlock before polling, re-acquire for register writes.

### Bug 3: Missing C2PMSG_37 write (`PspDriver.c:141-144`)
- Only low 32 bits written to C2PMSG_36. Firmware >4GB loses high bits.
- **Fix**: Write high 32 bits to C2PMSG_37 (0x10574).

### Bug 4: Shared g_CmdBuffer race (`PspDriver.c`)
- Single static buffer used by `PspInitTmr`, `RING_LOAD_IP_FW`, `REG_PROG`, `AUTOLOAD_RLC`.
- Concurrent IOCTLs corrupt each other's command data.
- **Fix**: Per-call dynamic allocation or per-thread buffer.

### Bug 5: LOAD_EMBEDDED_FW wrong alloc size (`PspDriver.c:881`)
- `MmAllocateContiguousMemory(g_SysdrvFirmwareSize, ...)` — uses SYSDRV size for SOS data.
- **Fix**: Use `g_SosFirmwareSize` for allocation.

### Bug 6: BOOT_SEQUENCE always returns STATUS_SUCCESS (`PspDriver.c:1007`)
- Even if SYSDRV/SOS fails, returns success. User must check results array manually.
- **Fix**: Return actual NTSTATUS from failing step.

### Bug 7: Test tool LoadFirmware reads wrong struct (`test-psp-driver.c:1910-1920`)
- Uses `PSP_LOAD_FW_RESPONSE` (2 ULONGs) but driver returns 1 ULONG.
- Second field (`MailboxStatus`) is garbage.
- **Fix**: Match test tool struct to driver output.

## Known Critical Bugs (GPU Driver Proxy — `src/kmd/amdbc250_psp.c`)

### Bug A: Orphaned code outside function (`amdbc250_psp.c:61-67`)
- `} else { ... return TRUE; } } return FALSE; }` — syntax error, does not compile.
- **Fix**: Delete lines 61-67.

### Bug B: `PSP_IOCTL_REG_PROG` not defined (`amdbc250_psp.c:83,108,151,184`)
- Used by KIQ functions but never defined in file or included from PspIoctl.h.
- **Fix**: Add `#define PSP_IOCTL_REG_PROG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, ...)`.

### Bug C: `g_GpcomRingPa` never saved from PSP_GET_GPU_INFO
- Reads response but never stores `gpuInfo.RingBufferPA` → `g_GpcomRingPa`.
- **Fix**: Save `g_GpcomRingPa = gpuInfo.RingBufferPA`, set `g_GpcomRingAvailable = TRUE`.

### Bug D: `g_KiqAvailable` never set to TRUE (RENAMED to `g_GpcomRingAvailable`)
- Was `g_KiqAvailable` (misleading name — variable held GPCOM ring status, not KIQ).
- **Fix**: Renamed to `g_GpcomRingAvailable`. Variable name now correctly reflects what it tracks.

## Known Broken Functions (GPU Driver Proxy)

### `Amdbc250PspKiqReadReg` — BROKEN, always returns 0xFFFFFFFF
- Writes a PM4 `COPY_DATA` packet into the **GPCOM ring** (which expects GPCOM commands, not PM4).
- The "doorbell" writes register 0 with value 20 via REG_PROG — does not ring any KIQ doorbell.
- **Replaced by**: `Amdbc250PspProxyReadReg` which uses `PSP_IOCTL_READ_REG` (direct PSP MMIO).

### `Amdbc250PspKiqSubmit` — BROKEN, returns STATUS_NOT_IMPLEMENTED
- Same issue: writes PM4 packets to GPCOM ring instead of a proper KIQ ring.
- A real KIQ ring needs: separate ring buffer allocation + proper doorbell register setup.
- **Current status**: Stubbed out. Not needed until KIQ ring is properly implemented.

## Read Path Architecture (GPU Register Access)

```
ioctl READ_REG/WRITE_REG
       │
       ▼
Amdbc250PspProxyAvailable()?
  ├── YES → Amdbc250PspProxyReadReg/WriteReg
  │          ├── READ: PSP_IOCTL_READ_REG (direct PSP MMIO) → ✅ works for all regs
  │          └── WRITE: PSP_IOCTL_REG_PROG
  │              ├── Via GPCOM ring (bypasses NBIO) → ❌ ring not supported
  │              ├── Via mailbox C2PMSG fallback → ❌ accepted but ignored
  │              └── Via direct BAR5 MMIO → ✅ if offset is correct
  └── NO  → DreamV3ReadRegister/WriteRegister (direct GPU MMIO)
              ├── GPU_ID, HDP, MMHUB, DF, NBIO → ✅ works at standard offsets
              ├── GC regs at old Navi10 offsets (0x2004 etc.) → ❌ 0xFFFFFFFF (unmapped)
              └── GC regs at BC-250 shifted offsets (0x3264 etc.) → ❓ needs re-test
```

## Current Status (June 2026)

### What Works
- PSP driver loads, boots SOS (firmware from BIOS v5 $PSP table)
- SOS is pre-loaded by BIOS — `C2PMSG_81=0xF0000010` even without BOOT_SEQUENCE
- GPU driver direct MMIO: GPU_ID, HDP, GC (at 0x3000), MMHUB, DF, NBIO registers
- PSP proxy reads: same values through PSP driver's BAR5 mapping
- GET_CAPS reports correct CUs=24, GPUCLK=2000 MHz
- GET_VRAM_INFO reports correct 16384 MB

### What Doesn't Work (may be wrong offsets, not NBIO)
- **GC registers at Navi10 offsets (0x2000-0x2FFF)**: Return 0xFFFFFFFF — but these are WRONG OFFSETS for BC-250 (missing GC_BASE shift 0x1260)
- **40 CU unlock IOCTL**: Writes to 0x2004 and 0x229C — needs correction to 0x3264 and 0x34FC
- **GPCOM ring creation**: SOS firmware doesn't support TOS ring protocol
- **Mailbox-based PROG_REG**: PSP accepts command but write silently ignored

### Key Findings
1. SOS is pre-loaded by BIOS/UEFI — our `BOOT_SEQUENCE` is redundant (but harmless)
2. Linux `psp_v11_0_8` driver skips ALL firmware loading and ALL command submission — SOS handles everything internally
3. NBIO firewall is activated by Windows boot process — cannot be unlocked from Windows
4. The only way to unlock NBIO is to cold boot from an environment where NBIO stays unlocked (Linux)
5. 40 CUs physically exist on the chip — factory disabled at the CC harvest level
6. `C2PMSG_64=0x00000000` always after ring creation attempt — SOS doesn't implement TOS ring commands (GFX_CTRL_CMD_ID_PROG_REG, etc.)

### Critical Finding 2026-06-11: BC-250 Has Different Register Map Than Navi10

**BC-250 (Cyan Skillfish) uses a non-standard BAR5 register layout.** Standard Navi10 has GC registers starting at BAR5+0x0000. BC-250 has GC registers at shifted offsets:

```c
// From linux/drivers/gpu/drm/amd/include/cyan_skillfish_ip_offset.h
GC_BASE__INST0_SEG0 = 0x00001260  // Segment 0: most GC registers
GC_BASE__INST0_SEG1 = 0x0000A000  // Segment 1: other GC registers
```

This means the actual BAR5 offset for a GC register is:
```
BAR5_offset = GC_BASE_SEG + register_offset
```

**Offset corrections for BC-250:**
| Register | Navi10 Offset | BC-250 Offset (SEG0=0x1260) |
|----------|--------------|-----------------------------|
| CC_GC_SHADER_ARRAY_CONFIG | 0x2004 | **0x3264** |
| GRBM_STATUS | 0x2000 | **0x3260** |
| SPI_PG_ENABLE_STATIC_WGP_MASK | 0x229C | **0x34FC** |
| RLC_PG_ALWAYS_ON_WGP_MASK | 0x2B04 | **0x3D64** |
| CP scratch (0x2074) | 0x2074 | **0x32D4** |
| SDMA (0x2600) | 0x2600 | **0x3860** |

**Impact:** Our Windows driver was reading registers at wrong offsets. All 0x2000-0x2FFF reads returned 0xFFFFFFFF because they hit unmapped address space, NOT because of NBIO firewall. The actual NBIO firewall status for GC registers on BC-250 is **unknown** — we need to re-test with corrected offsets.

**Linux amdgpu works because** `RREG32_SOC15/WREG32_SOC15` macros automatically add GC_BASE to register offsets. The 40 CU unlock patch (duggasco/bc250-40cu-unlock) works because it goes through this proper access path. PCI topology: `01:00.0 VGA compatible controller: Cyan Skillfish [BC-250]`, BAR5 at 0xFE800000 (512KB).

### Next Steps
1. **Re-test register reads with corrected offsets** in CachyOS Linux via devmem and in Windows via GPU driver
2. **Verify NBIO firewall hypothesis**: if 0x3264 returns valid value, NBIO might NOT be blocking GC registers at all
3. **Update Windows driver register offsets** for BC-250 (conditional on IPS-disabled chips)
4. **Test 40 CU unlock**: write 0xFFE00000 to BAR5+0x3264 and 0x1F to BAR5+0x34FC
5. **Warm reboot test**: if NBIO unlocked in Linux, verify persistence into Windows

## Hardware
- BC-250 (Cyan Skillfish) — mining chip, NOT PS5 console
- VRAM: 8GB (BIOS P4.00G) / 512MB (BIOS P3.00)
- BIOS P4.00G (American Megatrends, 2022-04-08) / P5.00 available
- ATOM BIOS: 113-AMDRBN-003

### PCI Topology
```
01:00.0 GPU      [1002:13FE] — BAR5: 0xFE800000 (512KB) MMIO
01:00.1 Audio    [1002:13FF]
01:00.2 PSP      [1022:143E] — BARs [disabled], I/O- Mem- BusMaster-
```

### Register Access
| Block | Access | Notes |
|-------|--------|-------|
| MMHUB (0x5000+) | Read/Write ✅ | Memory management |
| GC (0x3000-0x3008) | Read/Write ✅ | Graphics Core config |
| HDP (0x05A0+) | Read ✅, Write ❌ | Coherency |
| DF (0x1A000+) | Read ✅ | Data Fabric |
| NBIO (0xC100+) | Read/Write ✅ | Control registers |
| GPU_ID (0x0000) | Read ✅ | Returns 0x9FFF9700 |
| **GRBM (0x2004)** | ❓ | 0xFFFFFFFF — WRONG OFFSET for BC-250 (need +0x1260) |
| **CP/GRBM at correct offset** | ❓ | 0x3260+ — likely accessible, needs re-test |
| **SDMA (0x2600+)** | ❓ | Should be at 0x3860+ on BC-250 |
| **Scratch (0x2074+)** | ❓ | Should be at 0x32D4+ on BC-250 |
| **PSP C2PMSG** | ❌ | None blocked via PSP driver (separate BAR5 map) |

## NBIO Firewall
NBIO blocks CP/GRBM/SDMA registers. Hardware-level — cannot be bypassed from Windows.
Only PSP (separate ARM core) can unlock it.
On Linux, PSP firmware loaded during early kernel init (before NBIO activates). No equivalent on Windows.

## PSP Proxy (GPU Driver)
- `src/kmd/amdbc250_psp.c` — kernel-mode proxy that opens `\\.\AmdBcPsp` and sends IOCTLs
- Used for register access that bypasses NBIO firewall (via PSP/KIQ)
- Falls back to direct PSP MMIO (old psp_v11.c path) if proxy unavailable

## GPU Driver IOCTL Reference
| IOCTL | Code | Purpose |
|-------|------|---------|
| INIT_HARDWARE | 0x80000B80 | Map BAR5+BAR0 MMIO |
| READ_REG | 0x80000B88 | Read GPU register |
| WRITE_REG | 0x80000B8C | Write GPU register |
| READ_PCI_CONFIG | 0x80000BAC | Read PCI config (ECAM + IO ports) |
| WRITE_PCI_CONFIG | 0x80000BB0 | Write PCI config (IO ports) |
| SMN_ACCESS | 0x80000BC4 | SMN read/write (blocked) |
| GET_CAPS | 0x80000800 | Driver version, CU count |
| PSB_GET_STATUS | 0x80000BA4 | PSP init/SOS/NBIO status |

## PSP Driver IOCTL Reference
| IOCTL | Code | Purpose |
|-------|------|---------|
| PSP_READ_REG | 0x800 | Read GPU register via PSP |
| PSP_WRITE_REG | 0x801 | Write GPU register via PSP |
| PSP_LOAD_FW | 0x802 | Load firmware blob to contiguous memory |
| PSP_INIT_HW | 0x803 | Map BAR5 MMIO |
| PSP_NBIO_UNLOCK | 0x804 | Write NBIO signature registers |
| PSP_SEND_CMD | 0x805 | Send mailbox command (uses loaded FW) |
| PSP_CREATE_RING | 0x806 | Create PSP GPCOM ring |
| PSP_NBIO_VIA_RING | 0x807 | NBIO unlock via C2PMSG_64 |
| PSP_GET_STATUS | 0x808 | Full status snapshot |
| PSP_LOAD_EMBEDDED_FW | 0x809 | Load embedded SOS firmware |
| PSP_BOOT_SEQUENCE | 0x810 | Full boot: SYSDRV + SOS |
| PSP_PCI_READ | 0x811 | PCI config read |
| PSP_PCI_WRITE | 0x812 | PCI config write |
| PSP_PROBE | 0x813 | Comprehensive HW probe |
| PSP_RING_LOAD_IP_FW | 0x814 | Load GPU FW via ring |
| PSP_GET_GPU_INFO | 0x815 | Bridge info for GPU driver |
| PSP_REG_PROG | 0x816 | Program register via ring |
| PSP_AUTOLOAD_RLC | 0x817 | Trigger RLC autoload |
| PSP_KIQ_SUBMIT | 0x818 | KIQ ring submit (TODO) |
| PSP_INIT_TMR | 0x819 | Init Trusted Memory Region |

## Test Tools
- `output\safe-test.exe` — read-only GPU driver test
- `output\test-psp-init.exe` — GPU driver PSP status
- `output\test-psp-driver.exe` — PSP driver full test suite (all IOCTLs)
- GPU tests use `\\.\AMDBC250DreamV43`, PSP tests use `\\.\AmdBcPsp`

## Source Layout
```
GPU Driver (AMD-BC-250-Windows-Driver-main/)
  src/kmd/
    amdbc250_dream_kmd.c        # DriverEntry, IOCTL dispatch (~4739 lines)
    amdbc250_dream_hw_init.c    # GPU init, rings, display, PSP
    amdbc250_psp_v11.c          # Old PSP: BAR5 map, MP0 discovery, unlock
    amdbc250_psp.c              # PSP proxy: opens \\.\AmdBcPsp, KIQ stubs
    amdbc250_dream_power.c      # Power/thermal (SMU stubs)
    amdbc250_dream_vm.c         # GPUVM, GART, page tables
    firmware_data.h             # Embedded Navi10 firmware (SOS, ASD, TA)
  inc/
    amdbc250_dream_kmd.h        # Device extension, register defs
    amdbc250_dream_hw.h         # HW register definitions
    amdbc250_psp.h              # PSP proxy declarations + API
    amdbc250_ioctl.h            # IOCTL codes + structures
  docs/
    LINUX-AMDGPU-ANALYSIS.md    # Linux PSP v11.0_8 analysis
    NBIO-FIREWALL-ANALYSIS.md   # Register block map

PSP Driver (AMD-BC-250-PSP-Windows-Driver/)
  src/driver/
    PspDriver.c                 # Single-file WDM driver (~1305 lines)
  src/test/
    test-psp-driver.c           # User-mode test tool (~813 lines)
  inc/
    PspIoctl.h                  # Shared IOCTL codes + structs
    firmware_data.h             # Embedded firmware arrays (32KB+)
  inf/
    PspDriver.inf               # Device installation
```

## Gotchas
- VS paths hardcoded to E: — edit build.bat if VS is on C:
- build.bat prompts on error — may hang in CI/non-interactive use
- vkFreeMemory must NOT pass VirtualAlloc address to MmFreeContiguousMemory (BSOD)
- Do NOT map BAR5 beyond 512KB — crashes/hangs the system
- Reading unknown BAR5 offsets can freeze the system (hardware hang, requires reboot)
- PSP mailbox registers cause freeze when read directly from GPU driver
- The "Unexpected write to config offset b8" Linux message is NOT the NBIO unlock
- Spinlock held during mailbox polling blocks ALL other threads
- C2PMSG_81 save/restore clobbers PSP response — do NOT restore
- Both drivers share BAR5 — running MMIO tests with GPU driver active may cause black screen

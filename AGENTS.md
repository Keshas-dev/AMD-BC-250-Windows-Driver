# AMD BC-250 Windows Driver — Agent Notes

## Architecture
- **`atikmdag.sys`** — single driver, WDM control device + IOCTL channel
- Device path: `\\.\AMDBC250DreamV43`
- Not a real WDDM driver — `DxgkInitialize` never called (not exported on Win11 26100+)
- Coexists with Microsoft BasicDisplay (handles actual display output)

## Critical Constraint: NBIO Firewall
NBIO (Northbridge I/O) firewall blocks all CP/GRBM/SDMA registers. This is hardware-level — cannot be bypassed from Windows. Only the PSP (separate ARM core) can unlock it, but PSP mailbox registers are also blocked by the same firewall. **Chicken-and-egg problem.**

On Linux, PSP firmware is loaded during early kernel init (before NBIO activates). No equivalent mechanism exists on Windows.

## Build
- `build.bat` (requires VS2022 + WDK 10.0.26100.0 on E: or C: drive)
- **Run as Administrator** (required for cert trust to LocalMachine)
- KMD: `cl.exe /kernel` → `link.exe /DRIVER /ENTRY:DriverEntry` → `output\atikmdag.sys`
- UMD: `cl.exe /TP` → `link.exe /DLL` → `output\amdbc250umd64.dll`
- Test-signing: `bcdedit /set testsigning on` + reboot
- Secure Boot must be **OFF**
- Certificate: `CN=AMD-BC250-Signer` in LocalMachine\Root + LocalMachine\TrustedPublisher
- All 5 KMD .c files are compiled: `kmd.c` + `hw_init.c` + `power.c` + `vm.c` + `psp_v11.c`

## Install (ALWAYS uninstall first)
1. Device Manager → Uninstall (check "Delete driver") → **Reboot**
2. Device Manager → Update Driver → Browse to `output\` → **Reboot**

## Hardware
- BC-250 (Cyan Skillfish) — **mining** chip, NOT PS5 console
- Different PSP security fuses than PS5 (would accept standard AMD firmware if loadable)
- VRAM: **8GB** (BIOS P4.00G) / was 512MB (BIOS P3.00)
- BIOS P4.00G (American Megatrends, 2022-04-08)
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
| GPU_ID (0x0000) | Read ✅ | Returns 0xFFFF9714 |
| GRBM (0x2004) | ❌ | 0xFFFFFFFF |
| CP (0x2000-0x2FFF) | ❌ | All blocked |
| SDMA (0x2600+) | ❌ | All blocked |
| Scratch (0x2074+) | ❌ | 0xFFFFFFFF |
| PSP C2PMSG | ❌ | All blocked by NBIO |

## PSP Integration (compiled into atikmdag.sys)
- `src/kmd/amdbc250_psp_v11.c` — PSB BAR5 mapping, MP0 discovery, ring stubs
- PSP init runs as Step 9 in DreamV3HwInitialize (non-fatal)
- NBIO unlock attempted if SOS detected (always fails — PSP unreachable)
- Register offsets from `mp_11_0_8_offset.h` (Navi10-compatible)
- All operations are non-fatal — driver continues without PSP

## IOCTL Reference
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

## Test Tools
- `output\safe-test.exe` — read-only, safe (run first after install)
- `output\test-psp-init.exe` — PSP status (PspInitialized, SosAlive, NbioUnlocked)
- `output\cp-write-test.exe` — CP register write accessibility test
- All tests use `\\.\AMDBC250DreamV43`

## Source Layout
```
src/kmd/
  amdbc250_dream_kmd.c        # DriverEntry, IOCTL dispatch (~4739 lines)
  amdbc250_dream_hw_init.c    # GPU init, rings, display, PSP
  amdbc250_psp_v11.c          # PSP: BAR5 map, MP0 discovery, unlock
  amdbc250_dream_power.c      # Power/thermal (SMU stubs)
  amdbc250_dream_vm.c         # GPUVM, GART, page tables
  firmware_data.h             # Embedded Navi10 firmware (SOS, ASD, TA)
inc/
  amdbc250_dream_kmd.h         # Device extension, register defs
  amdbc250_dream_hw.h          # HW register definitions
  amdbc250_psp_v11.h           # PSP context + API
  amdbc250_ioctl.h             # IOCTL codes + structures
docs/
  LINUX-AMDGPU-ANALYSIS.md     # Linux PSP v11.0_8 analysis
  NBIO-FIREWALL-ANALYSIS.md    # Register block map
```

## Gotchas
- VS paths hardcoded to E: — edit build.bat if VS is on C:
- build.bat prompts on error — may hang in CI/non-interactive use
- vkFreeMemory must NOT pass VirtualAlloc address to MmFreeContiguousMemory (BSOD)
- Do NOT map BAR5 beyond 512KB — crashes/hangs the system
- Reading unknown BAR5 offsets can freeze the system (hardware hang, requires reboot)
- PSP mailbox registers cause freeze when read — no known safe read method
- The "Unexpected write to config offset b8" Linux message is NOT the NBIO unlock

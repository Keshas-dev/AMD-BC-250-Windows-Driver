# AMD BC-250 Windows Driver — Project Overview & Instructions

## What This Project Is

AMD BC-250 (Cyan Skillfish) GPU driver for Windows 11. The hardware is a **mining** variant of the PS5 Oberon chip — **NOT** a PS5 console. The PSP has different security keys (mining fuses), meaning standard AMD-signed PSP firmware would be accepted if we could load it.

## Current Architecture

Two drivers run in parallel:
- **`atikmdag.sys`** — Main GPU driver (WDM control device + WDDM stubs). PSP logic integrated.
- **`PspDriver.sys`** — Separate project at `C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver`

The GPU driver creates `\\.\AMDBC250DreamV43` for IOCTL communication.

## Hardware Access

- **BAR5**: `0xFE800000` (512KB) — MMIO registers ✅
- **BAR0**: `0xC0000000` (256MB) — VRAM window ✅
- **BAR2**: `0xD0000000` (2MB) — Doorbell ✅

### Register Access Status

| Block | Offset | Read | Write | Status |
|-------|--------|------|-------|--------|
| GPU_ID | 0x0000 | ✅ | N/A | 0xFFFF9714 |
| HDP | 0x05A0+ | ✅ | ❌ ignored | |
| GC | 0x3000-0x3008 | ✅ | ✅ | Limited range |
| GRBM | 0x2004 | ❌ | ❌ | 0xFFFFFFFF (blocked) |
| CP | 0x2000-0x2FFF | ❌ | ❌ | NBIO firewall |
| SDMA | 0x2600+ | ❌ | ❌ | NBIO firewall |
| Scratch | 0x2074+ | ❌ | ❌ | 0xFFFFFFFF |
| MMHUB | 0x5000-0x59D0 | ✅ | ✅ | Full access |
| DF | 0x1A000+ | ✅ | ❌ | Data Fabric |
| NBIO | 0xC100-0xC1FC | ✅ | ✅ | Control registers |
| PSP mailboxes | various | ❌ | ❌ | NBIO blocks all |

### The NBIO Problem

**NBIO firewall blocks all CP/GRBM/SDMA registers.** GPU compute is impossible without bypassing this. The firewall is hardware-level — no software bypass exists from Windows.

Only the PSP (Platform Security Processor) can unlock NBIO. The PSP is a separate ARM core with Ring -4 privileges. It runs Secure OS (SOS) firmware that configures NBIO registers.

**Chicken-and-egg problem:**
- NBIO unlock requires PSP communication
- PSP communication goes through C2PMSG mailbox registers
- These registers are BLOCKED by NBIO
- Cannot reach PSP → cannot unlock NBIO

## How Linux Solves It

Linux loads PSP firmware from `/lib/firmware/amdgpu/` during kernel init. The firmware is loaded by the kernel BEFORE the NBIO firewall can block access (early boot, before PCI enumeration completes). Once PSP SOS runs, it unlocks NBIO, and then CP/GRBM/SDMA become accessible.

On Windows, there is no equivalent mechanism to load firmware before PCI enumeration. The NBIO is already active by the time our driver loads.

## BIOS Information

| Version | Features | 
|---------|----------|
| **P3.00** (original) | 512MB VRAM, PSP disabled |
| **P4.00G** (current) | **8192MB VRAM** (8GB), PSP disabled |

ATOM BIOS version remains `113-AMDRBN-003` on both. PSP function (01:00.2) has disabled BARs (`I/O- Mem- BusMaster-`, IRQ 255) regardless of BIOS version.

## Build Instructions

### Prerequisites
- Visual Studio 2022 Community/Professional
- WDK 10.0.26100.0
- Test signing: `bcdedit /set testsigning on`
- Secure Boot: **DISABLED** (must be off in BIOS)
- Certificate: `CN=AMD-BC250-Signer` in LocalMachine\Root + TrustedPublisher

### Build
```cmd
build.bat
```
Run as **Administrator** (required for certificate trust).

### Install
```cmd
Device Manager → Uninstall (check "Delete driver") → Reboot
Device Manager → Update driver → Browse to output\ → Install → Reboot
```

### Test
```cmd
output\safe-test.exe         # Basic IOCTL tests (read-only)
output\test-psp-init.exe     # PSP status check
output\cp-write-test.exe     # CP register write test
```

## Key Findings

1. **PSP function (01:00.2) is DISABLED** — both BARs disabled, no driver bound, IRQ 255
2. **PSP firmware doesn't exist for Cyan Skillfish2** — no `cyan_skillfish2_sos.bin` released by AMD
3. **Generic PSP firmware (navi10_*.bin) fails** — signature mismatch per AMD driver logs
4. **PCI config writes to root complex offset 0xB8 work** but are NOT the NBIO unlock (happens after rings are created on Linux)
5. **NBIO unlock requires PSP** — no alternative path from Windows
6. **Writing to 0xC100/0xC180** with magic values doesn't unlock NBIO (writes are silently discarded)

## What Was Fixed (v4.3 bug fixes)

- PSP MDL memory management (save MDL pointer, proper cleanup)
- DreamV3DdiDestroyAllocation now frees physical memory (was leaking)
- Amdbc250PspValidateFirmware logic fixed (was always failing)
- SDMA ring wrap fills NOPs before wrapping
- Fixed-point arithmetic in kernel mode (was using float)
- INF SupportIcd=0 → 1 (D3D12 support)
- Null pointer guards in Present path
- Version consistency (4.3.0.0 everywhere)
- Build.bat signing improvements

## Remaining Issues

1. **NBIO firewall** — no bypass found, PSP communication blocked
2. **DxgkInitialize never called** — this is NOT a real WDDM driver, it's a WDM IOCTL driver
3. **SDMA/CP rings inaccessible** — no GPU compute possible
4. **VRAM reporting** — Windows reports 16GB but actual VRAM is 8GB (Linux shows correct value)

## Future Paths

1. **BIOS modification** (UEFITool) — inject PSP firmware loading into BIOS
2. **Find `cyan_skillfish2_sos.bin`** — if AMD ever releases this firmware
3. **PSP driver** (`PspDriver.sys`) — separate project for D5->SMU unlock
4. **Software rendering** — CPU-based fallback (slow but functional)

## File Structure

```
src/kmd/                     # Kernel-Mode Driver
  amdbc250_dream_kmd.c       # DriverEntry, IOCTL dispatch, WDDM callbacks
  amdbc250_dream_hw_init.c   # GPU init, rings, display, PSP (Step 9)
  amdbc250_psp_v11.c         # PSP: BAR5 map, MP0 discovery, rings, NBIO unlock
  amdbc250_dream_power.c     # Power/thermal management (stubs)
  amdbc250_dream_vm.c        # GPUVM, GART, page tables
  firmware_data.h            # Embedded Navi10 firmware (SOS, ASD, TA)
src/umd/                     # User-Mode Driver (D3D9)
src/vulkan/                  # Vulkan ICD
inc/                         # Shared headers
test-tools/                  # Test programs
third-party/                 # Linux boot logs, firmware files
output/                      # Build output (signed)
docs/                        # Analysis documents
inf/                         # Driver INF
```

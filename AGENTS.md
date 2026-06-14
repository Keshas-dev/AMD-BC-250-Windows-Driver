# AMD BC-250 Windows Driver — Agent Notes

## Architecture
- **Two-driver hybrid model**: GPU driver (`atikmdag.sys`) + PSP driver (`PspDriver.sys`) run in parallel.
- GPU driver: `\\.\AMDBC250DreamV43`, WDM control device + IOCTL channel
- PSP driver: `\\.\AmdBcPsp`, separate kernel driver for PSP firmware/ring control
- Not real WDDM — `DxgkInitialize` never called (not exported on Win11 26100+)
- Coexists with Microsoft BasicDisplay (handles actual display output)

## PSP Driver Repo
`C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\`
- Kernel: `src\driver\PspDriver.c` (~1465 lines)
- IOCTL defines: `inc\PspIoctl.h`
- Test tool: `src\test\test-psp-driver.c` (~840 lines)
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

## PSP Driver Bug Status (ALL FIXED ✅)

### Bug 1: C2PMSG_81 save/restore — ✅ FIXED
- Removed save/restore in `PspSendMailboxCommand()`. Now polls C2PMSG_35 until clear.

### Bug 2: Spinlock held during polling — ✅ FIXED
- `KeReleaseSpinLock` called before polling loop. Other threads not blocked.

### Bug 3: Missing C2PMSG_37 write — ✅ FIXED
- High 32 bits written to C2PMSG_37 (offset 0x10574) for 64-bit PA support.

### Bug 4: Shared g_CmdBuffer race — ✅ FIXED
- Per-call allocation via `ExAllocatePool2` for ring commands. Global `g_RingBuffer` protected by spinlock.

### Bug 5: LOAD_EMBEDDED_FW wrong alloc size — ✅ FIXED
- SOS allocation uses 256KB (262144 bytes), separate from SYSDRV size.

### Bug 6: BOOT_SEQUENCE always returns STATUS_SUCCESS — ✅ FIXED
- Returns `stepStatus` from failing step. Error codes propagated correctly.

### Bug 7: Test tool LoadFirmware reads wrong struct — ✅ FIXED
- Driver returns single ULONG (PA>>20). Test tool updated to match.

## GPU Driver Proxy Bug Status (ALL FIXED ✅)

### Bug A: Orphaned code outside function — ✅ FIXED
- Stray `} else { ... }` block removed.

### Bug B: PSP_IOCTL_REG_PROG not defined — ✅ FIXED
- `#define PSP_IOCTL_REG_PROG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, ...)` added.

### Bug C: g_GpcomRingPa never saved — ✅ FIXED
- `g_GpcomRingPa = gpuInfo.RingBufferPA` added after PSP_GET_GPU_INFO call.

### Bug D: g_KiqAvailable never set to TRUE — ✅ FIXED
- Renamed to `g_GpcomRingAvailable`. Set when ring PA is non-zero.

## KIQ Ring Implementation (GPU Driver — `amdbc250_psp.c`)

### What was added to `amdbc250_psp.c`:
- **KIQ ring variables**: `g_KiqRingVa`, `g_KiqRingPa`, `g_KiqRingSize`, `g_KiqRingWptr`, `g_KiqRingLock`
- **`Amdbc250PspKiqInit()`**: Allocates 8KB contiguous ring buffer, writes PA to KIQ_BASE_LO/HI, enables KIQ_CNTL
- **`Amdbc250PspKiqSubmit()`**: Submits PM4 packets to KIQ ring with spinlock protection
- **`Amdbc250PspKiqReadReg()`**: Delegates to `Amdbc250PspProxyReadReg()` (PSP direct MMIO)
- **`Amdbc250PspKiqCleanup()`**: Frees KIQ ring buffer, disables ring
- **Integration**: KIQ init called from `PspProxyInit()` and `DreamV3HwInitialize()` step 9c
- **Device extension**: Added `KiqAvailable` field to `DREAM_V3_DEVICE_EXTENSION`

## KIQ Ring Implementation (PSP Driver — `PspDriver.c`) — NEW 2026-06-14

### What was added to `PspDriver.c`:
- **`PspKiqInit()`**: Allocates 4KB contiguous ring, configures KIQ_BASE_LO/HI via BAR5 MMIO (does NOT write KIQ_CNTL — self-clearing)
- **`PspKiqSubmit()`**: Writes PM4 DWORDs to ring buffer at `g_KiqRingWptr`, ring doorbell via KIQ_WPTR (0xE078) write
- **`PspKiqCleanup()`**: Frees ring buffer in DriverUnload
- **IOCTL handler**: `IOCTL_PSP_KIQ_SUBMIT` (0x818) — accepts array of PM4 DWORDs, returns new WPTR
- **Protection**: Spinlock (`g_KiqRingLock`) guards ring buffer writes and WPTR updates

### KIQ Register Map (BC-250, GC_BASE=0x1260) — UPDATED 2026-06-13
| Register | BAR5 Offset | Navi10 Native | Writable? | Notes |
|----------|-------------|----------------|-----------|-------|
| GRBM_GFX_INDEX | 0x34D0 | 0x224C | ✅ | Must set ME=1 first (0x00010000) |
| KIQ_BASE_LO | 0xE060 | 0xCE00 | ✅ | Via PSP_WRITE_REG — value sticks |
| KIQ_BASE_HI | 0xE064 | 0xCE04 | ✅ | Via PSP_WRITE_REG |
| KIQ_CNTL | 0xE068 | 0xCE08 | ⚠️ | Write accepted, reads back 0 (self-clearing?) |
| KIQ_RPTR | 0xE06C | 0xCE0C | ❌ | Read-only even with GRBM=ME1 |
| KIQ_WPTR | 0xE078 | 0xCE18 | **✅** | **WRITABLE via PSP_WRITE_REG** — key doorbell! |

### Critical Finding UPDATE (2026-06-13): KIQ_WPTR is WRITABLE via PSP_WRITE_REG!
- **KIQ_WPTR (0xE078) is WRITABLE when accessed via PSP driver's BAR5 MMIO** — previous "read-only" finding was from GPU driver using wrong MMIO mapping (GPU driver wrote to system memory instead of hardware registers)
- KIQ_WPTR value persists after write-back read ✅ — this is the doorbell we needed!
- KIQ_CNTL write accepted but reads back as 0 (may be write-only or self-clearing)
- **KIQ doorbell mechanism**: Write KIQ_WPTR (0xE078) via PSP_WRITE_REG to trigger GPU execution
- CP_RING0_BASE_LO (0xDA60) remains read-only by hardware — cannot use GFX ring
- **KIQ ring + KIQ_WPTR doorbell is the viable submission path****

## Test Results (2026-06-13)

| Test | Result | Details |
|------|--------|---------|
| PSP Init | ✅ | BAR5 mapped at VA=0x0D300000 |
| PSP Alive (C2PMSG_81) | ✅ | 0xF0000010 — SOS running |
| NBIO Unlock | ✅ | SIG1=0xFEDCBAEF, SIG2=0xFEDCBADF |
| Boot Sequence | ✅ | SYSDRV(0x4)=SENT, SOS(0x8)=SENT, GRBM=0x0 |
| GPU_ID | ✅ | 0x9FFF9700 |
| GET_CAPS | ✅ | CUs=24, GPUCLK=2000 MHz |
| GET_VRAM_INFO | ✅ | 16384 MB |
| CP Scratch (0x32D4) | ✅ | 0x4D585042 ("MXPB" — CP alive) |
| SPI_PG_ENABLE (0x34FC) | ✅ | Writable: 0x2000→0x3F00→0x2000 |
| KIQ_BASE_LO (0xE060) | ✅ | Writable with GRBM=ME1: 0x12345000 read back |
| KIQ_WPTR (0xE078) | ✅ | **WRITABLE via PSP_WRITE_REG** — value persists! (was misreported as read-only) |
| KIQ_CNTL (0xE068) | ⚠️ | Write accepted, reads back 0 (self-clearing?) |
| KIQ_RPTR (0xE06C) | ❌ | Read-only even with GRBM=ME1 |
| CC_GC_SHADER_ARRAY_CONFIG | ✅ | Readable: 0x0 |
| MMHUB | ✅ | 10 registers readable |
| DF | ✅ | 3 registers readable |
| REG_PROG | ✅ | PSP accepts command (response=val) |
| Embedded FW Load | ✅ | PA>>20=0x0000448D |
| GPU Bridge Info | ✅ | RingBufferPA=0x9AC86100, TMR=0xF40F800000 |
| Ring Creation | ❌ | SOS doesn't support TOS ring protocol (C2PMSG_64 never sets bit 31) |
| SMU | ❌ | No response (power-gated, no SMU firmware on this silicon) |
| KIQ_SUBMIT IOCTL | ✅ | Implemented in PspDriver.c (PspKiqInit/Submit/Cleanup) |
| Test tool -k option | ✅ | Builds PM4 NOP submission via KIQ_SUBMIT IOCTL |

## Read Path Architecture (GPU Register Access)

```
ioctl READ_REG/WRITE_REG
       │
       ▼
Amdbc250PspProxyAvailable()?
  ├── YES → Amdbc250PspProxyReadReg/WriteReg
  │          ├── READ: PSP_IOCTL_READ_REG (direct PSP MMIO) → ✅ works for all regs
  │          └── WRITE: PSP_IOCTL_WRITE_REG (direct PSP MMIO) → ✅ for GC regs at correct offsets
  └── NO  → DreamV3ReadRegister/WriteRegister (direct GPU MMIO)
              ├── GPU_ID, HDP, MMHUB, DF, NBIO → ✅ works at standard offsets
              ├── GC regs at old Navi10 offsets (0x2004 etc.) → ❌ 0xFFFFFFFF (unmapped)
              └── GC regs at BC-250 shifted offsets (0x3264 etc.) → ✅ confirmed working

NOTE: PSP proxy BYPASSED (commit 7eec13a). All READ_REG/WRITE_REG IOCTLs
now use DreamV3ReadRegister/WriteRegister directly. PSP proxy mapped
PSP BAR0 (0xFD600000) which returns 0xFFFFFFFF for non-PSP registers.
Direct GPU BAR5 (0xFE800000) MMIO works correctly for all accessible registers.
```

## Current Status (2026-06-13)

### What Works
- PSP driver loads, boots SOS (firmware from BIOS v5 $PSP table)
- SOS is pre-loaded by BIOS — `C2PMSG_81=0xF0000010` even without BOOT_SEQUENCE
- GPU driver direct MMIO: GPU_ID, HDP, GC, MMHUB, DF, NBIO registers
- **GC registers at BC-250 corrected offsets (0x3260+)** — GRBM=0x0, CC=0x0, SPI=0x2000, Scratch=0x4D585042
- PSP proxy reads: same values through PSP driver's BAR5 mapping
- GET_CAPS reports correct CUs=24, GPUCLK=2000 MHz
- GET_VRAM_INFO reports correct 16384 MB
- **SPI writes at 0x34FC (GC_BASE+0x229C)** — 0x2000→0x3F00→0x2000 restore works ✅
- **KIQ_BASE_LO at 0xE060** — writable with GRBM=ME1 ✅
- **CP ring CNTL/RPTR/WPTR at 0xDA68/0xDA6C/0xDA78** — WRITABLE ✅
- **KIQ_WPTR (0xE078)** — **WRITABLE via PSP_WRITE_REG** (key doorbell!) ✅
- **All PSP driver bug fixes (#1-7) applied** ✅
- **All GPU driver proxy bug fixes (A-D) applied** ✅
- **KIQ ring implementation added in PSP driver (PspDriver.c)** ✅
- **KIQ_SUBMIT IOCTL (0x818) implemented** — allocates ring, submits PM4 dwords, rings doorbell via KIQ_WPTR
- **Test tool -k option added** — sends PM4 commands through KIQ_SUBMIT IOCTL

### What Doesn't Work (BLOCKERS for 3D/compute)
- **KIQ_CNTL (0xE068)** — READ-ONLY (cannot enable KIQ ring)
- **KIQ_RPTR/WPTR (0xE06C/0xE078)** — READ-ONLY (cannot ring doorbell)
- **GFX ring BASE_LO (0xDA60)** — READ-ONLY (cannot set ring buffer address)
- **CP_ME_CNTL/MEC_CNTL (0xC060/0xC0E0)** — NBIO blocks writes
- **GPCOM ring creation** — SOS doesn't support TOS ring protocol
- **Mailbox PROG_REG** — PSP accepts command but write silently ignored
- **SMU** — power-gated, no firmware response

### What Is Needed for 3D
1. **Working command channel** (ring + doorbell) → PM4 packets → GPU execution
2. **Ring BASE register** must be writable + CNTL must enable ring + doorbell must trigger execution
3. **At minimum**: KIQ_CNTL writable OR alternative doorbell mechanism
4. **SMU wake** would enable more WGP/CU but is not strictly required for basic command submission

### Key Findings
1. SOS is pre-loaded by BIOS/UEFI — our `BOOT_SEQUENCE` is redundant (but harmless)
2. Linux `psp_v11_0_8` driver skips ALL firmware loading and ALL command submission — SOS handles everything internally
3. NBIO does NOT block GC registers on BC-250 at corrected offsets — all previous 0xFFFFFFFF reads were due to wrong offsets
4. 40 CUs physically exist on the chip — factory disabled at the CC harvest level
5. `CC_GC_SHADER_ARRAY_CONFIG` = 0x0 means all CUs disabled at array level; `SPI_PG_ENABLE_STATIC_WGP_MASK` = 0x2000 (only WGP 5 enabled)
6. `C2PMSG_64=0x00000000` always after ring creation attempt — SOS doesn't implement TOS ring commands
7. **NBIO blocks all writes to 0xC000+ range from ALL paths** (direct GPU BAR5, PSP BAR0, SMN, GC_BASE aliased addresses where native offset is 0xC000+)
8. **KIQ_BASE_LO at 0xE060 is the ONLY writable ring BASE** — but KIQ_CNTL/RPTR/WPTR remain read-only
9. KIQ_RPTR/WPTR read-only even with GRBM_GFX_INDEX=0x00010000 (ME=1, PIPE=0, QUEUE=0)

### Critical Finding: KIQ_WPTR IS WRITABLE via PSP driver's BAR5 MMIO!
- Previous testing from GPU driver failed because it wrote to system memory (`g_KiqRingVa + offset`) instead of hardware registers (BAR5)
- When accessed through PSP driver's PSP_WRITE_REG, KIQ_WPTR (0xE078) accepts writes and value persists ✅
- KIQ_CNTL (0xE068) write accepted but reads back as 0 (self-clearing)
- **KIQ doorbell mechanism**: Write KIQ_WPTR via PSP_WRITE_REG to trigger GPU execution

### Critical Finding: BC-250 Has Different Register Map Than Navi10

**BC-250 (Cyan Skillfish) uses a non-standard BAR5 register layout.** Standard Navi10 has GC registers starting at BAR5+0x0000. BC-250 has GC registers at shifted offsets:

```c
// From linux/drivers/gpu/drm/amd/include/cyan_skillfish_ip_offset.h
GC_BASE__INST0_SEG0 = 0x00001260  // Segment 0: most GC registers
GC_BASE__INST0_SEG1 = 0x0000A000  // Segment 1: other GC registers
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

### CONFIRMED — Linux CachyOS devmem test results:
- `0xFE803264` (CC_GC_SHADER_ARRAY_CONFIG) = **0x0** — readable, NBIO NOT blocking
- `0xFE8034FC` (SPI_PG_ENABLE_STATIC_WGP_MASK) = **0x2000** — readable, NBIO NOT blocking
- Conclusion: **NBIO does NOT block GC registers on BC-250 at corrected offsets**

### SMU v11.8 Mailbox Offsets (from `mp_11_0_8_offset.h` + `cyan_skillfish_ip_offset.h`)

MP1_BASE__INST0_SEG0 = **0x16000** (byte offset in BAR5, same as MP0_BASE on BC-250)

| Register | mm (DWORD) | BAR5 offset (byte) | Purpose |
|----------|-----------|-------------------|---------|
| C2PMSG_66 | 0x0282 | **0x16A08** | Message register (write msg → triggers SMU) |
| C2PMSG_82 | 0x0292 | **0x16A48** | Argument register (write param, read result) |
| C2PMSG_83 | 0x0293 | **0x16A4C** | Extended data |
| C2PMSG_90 | 0x029A | **0x16A68** | Response register (0=busy, 1=OK, FF=err) |

### SMU v11.8 Protocol (from Linux `smu_cmn.c`)
1. Write 0 → C2PMSG_90 (clear response)
2. Write param → C2PMSG_82 (argument)
3. Write msg → C2PMSG_66 (trigger SMU)
4. Poll C2PMSG_90 until !0 (1=OK, 0xFF=Failed)
5. Read result from C2PMSG_82

**BC-250 SMU does NOT respond** — likely power-gated or lacks SMU firmware on this silicon revision.

### THM Base (Thermal sensor) — CORRECTION
**Confirmed via write-back test: THM_BASE = 0x8000** (not 0x16600).
- `THM_CTRL [0x8000]` = 0x18, writable
- `THM_CURR [0x8008]` = 0x08, read-only (temperature)

### Current Plan
1. ~~**GC_BASE=0x1260 applied**~~ ✅
2. ~~**NBIO NOT blocking confirmed**~~ ✅  
3. ~~**THM corrected to 0x8000**~~ ✅
4. ~~**Test WGP/CC writes**~~ ✅ — SPI at 0x34FC writable
5. ~~**PSP driver bug fixes (#1-7)**~~ ✅
6. ~~**GPU driver proxy fixes (A-D)**~~ ✅
7. ~~**KIQ ring implementation**~~ ✅ (code added, but CNTL/RPTR/WPTR read-only)
8. ~~**Find KIQ doorbell mechanism**~~ ✅ — KIQ_WPTR (0xE078) IS WRITABLE via PSP_WRITE_REG!
9. ~~**Test PSP_REG_PROG with real GPU register**~~ ✅ — no-op, use PSP_WRITE_REG instead
10. ~~**Implement KIQ_SUBMIT IOCTL in PSP driver**~~ ✅ — PspKiqInit/Submit/Cleanup + IOCTL handler
11. **SMU wake** — try SMN access or PSP commands to power-gate SMU on
12. **Test KIQ_SUBMIT** — submit PM4 NOPs, verify execution via CP scratch

## Hardware
- BC-250 (Cyan Skillfish) — mining chip, NOT PS5 console
- VRAM: 16GB (BIOS P4.00G) / 512MB (BIOS P3.00)
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
| GRBM_STATUS (0x3260) | Read ✅ | = 0x0 |
| CC_GC_SHADER_ARRAY_CONFIG (0x3264) | Read ✅ | = 0x0 |
| SPI_PG_ENABLE_STATIC_WGP_MASK (0x34FC) | Read ✅/Write ✅ | = 0x2000, writable |
| RLC_PG_ALWAYS_ON_WGP_MASK (0x3D64) | Read ✅ | BC-250 offset |
| Scratch (0x32D4) | Read ✅ | = 0x4D585042 (CP alive) |
| CP_RING0_BASE_LO (0xDA60) | Read only ❌ | Hardware read-only |
| CP_RING0_CNTL/RPTR/WPTR (0xDA68-0xDA78) | Write ✅ | But BASE_LO read-only |
| KIQ_BASE_LO (0xE060) | Write ✅ | With GRBM=ME1 |
| KIQ_CNTL (0xE068) | ⚠️ | Write accepted, reads back 0 (self-clearing?) |
| KIQ_RPTR (0xE06C) | Read only ❌ | Even with GRBM=ME1 |
| KIQ_WPTR (0xE078) | Write ✅ | **WRITABLE via PSP_WRITE_REG** — doorbell mechanism! |
| CP_ME_CNTL (0xC060) | Read only ❌ | NBIO address, writes ignored |

## NBIO Firewall
NBIO on BC-250 does NOT block GC/GRBM/SDMA registers at corrected offsets (confirmed via Linux devmem).
NBIO blocks writes to 0xC000+ range from ALL paths — direct GPU BAR5, PSP BAR0, SMN, GC_BASE aliased addresses where native offset is 0xC000+.
GC_BASE aliases (0xDA60+) bypass NBIO for registers where native offset is 0xC800+ — but some registers (BASE_LO) are still hardware read-only.

## PSP Proxy (GPU Driver)
- `src/kmd/amdbc250_psp.c` — kernel-mode proxy that opens `\\.\AmdBcPsp` and sends IOCTLs
- Used for register access that bypasses NBIO firewall (via PSP/KIQ)
- Falls back to direct PSP MMIO (old psp_v11.c path) if proxy unavailable
- **KIQ functions now implemented** (not just stubs)

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
| PSP_KIQ_SUBMIT | 0x818 | KIQ ring submit |
| PSP_INIT_TMR | 0x819 | Init Trusted Memory Region |

## Test Tools
- `output\safe-test.exe` — read-only GPU driver test
- `output\test-psp-init.exe` — GPU driver PSP status
- `output\test-psp-driver.exe` — PSP driver full test suite (all IOCTLs)
- GPU tests use `\\.\AMDBC250DreamV43`, PSP tests use `\\.\AmdBcPsp`

## Test Tool Options (test-psp-driver.exe)
| Option | Arguments | Purpose |
|--------|-----------|---------|
| `-i` | PA size | Init hardware (map BAR5) |
| `-s` | | Get PSP status |
| `-B` | | Boot sequence (SYSDRV+SOS) |
| `-E` | | Load embedded firmware |
| `-u` | | NBIO unlock |
| `-R` | | Create ring |
| `-G` | | Get GPU bridge info |
| `-r` | offset | Read register |
| `-w` | offset value | Write register |
| `-C` | command | Send mailbox command |
| `-P` | regId value | REG_PROG (program register) |
| `-m` | | SMU mailbox status |
| `-L` | type filename | Load IP firmware via ring |
| `-A` | | Autoload RLC |
| `-T` | | Init TMR |
| `-t` | | Comprehensive test |
| `-H` | | Hardware info |
| `-M` | | Memory test |
| `-pb` | bus devfn offset | PCI config read |
| `-pw` | bus devfn offset val | PCI config write |
| `-k` | dwords... | Submit PM4 commands via KIQ ring (up to 64 hex dwords) |
| `-l` | logfile | Log to file |

## Source Layout
```
GPU Driver (AMD-BC-250-Windows-Driver-main/)
  src/kmd/
    amdbc250_dream_kmd.c        # DriverEntry, IOCTL dispatch
    amdbc250_dream_hw_init.c    # GPU init, rings, display, PSP, KIQ init
    amdbc250_psp_v11.c          # Old PSP: BAR5 map, MP0 discovery, unlock
    amdbc250_psp.c              # PSP proxy + KIQ ring implementation
    amdbc250_dream_power.c      # Power/thermal (SMU stubs)
    amdbc250_dream_vm.c         # GPUVM, GART, page tables
    firmware_data.h             # Embedded Navi10 firmware (SOS, ASD, TA)
  inc/
    amdbc250_dream_kmd.h        # Device extension, register defs, KiqAvailable
    amdbc250_dream_hw.h         # HW register definitions
    amdbc250_psp.h              # PSP proxy declarations + KIQ API
    amdbc250_ioctl.h            # IOCTL codes + structures
  docs/
    LINUX-AMDGPU-ANALYSIS.md    # Linux PSP v11.0_8 analysis
    NBIO-FIREWALL-ANALYSIS.md   # Register block map

PSP Driver (AMD-BC-250-PSP-Windows-Driver/)
  src/driver/
    PspDriver.c                 # Single-file WDM driver (~1465 lines, all bugs fixed)
  src/test/
    test-psp-driver.c            # User-mode test tool (~840 lines)
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
- C2PMSG_81 save/restore clobbers PSP response — do NOT restore (now fixed in code)
- Both drivers share BAR5 — running MMIO tests with GPU driver active may cause black screen
- GC_BASE_SHIFTED ring addresses (0xDA60+) bypass NBIO firewall (0xC800+ native) but BASE registers remain read-only by hardware
- KIQ at 0xE060 is the ONLY writable ring BASE — but KIQ_CNTL/RPTR/WPTR are also read-only
- **Do NOT write 0xDEADBEEF to KIQ_BASE_LO** — GPU hangs, requires reboot. Use page-aligned addresses only.
- **KIQ_WPTR is only writable through PSP driver's BAR5 MMIO** (PSP_WRITE_REG), NOT through GPU driver's direct MMIO
- **KIQ_CNTL may be self-clearing** — write accepted, reads back 0, but may still enable the ring

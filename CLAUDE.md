# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Commands

### Building the Driver
- **Full build (driver + signing)**: `build.bat`  
  Requires: Visual Studio 2022, Windows WDK 10.0.26100.0, test signing enabled (`bcdedit /set testsigning on`), and a trusted self-signed certificate (`CN=AMD-BC250-Signer` in LocalMachine\Root and TrustedPublisher).
- **Build test tools individually** (located in `test-tools/`):
  - `compile-safe.bat` – builds `safe-test.exe` (read-only sanity check)
  - `compile-deep.bat` – builds `deep-test.exe` (register scan + write test)
  - `compile-wddm.bat` – builds `test-wddm.exe` (full WDDM + IOCTL test suite)
  - `compile-psp-init.bat` – builds `test-psp-init.exe` (PSP status check)
- **Clean artifacts**: delete `obj\`, `output\`, and rebuild.

### Installing & Testing
1. **Uninstall existing driver** (critical):  
   Device Manager → AMD Radeon BC-250 → Uninstall device (check "Delete driver") → Reboot.
2. **Install new driver**:  
   Device Manager → AMD Radeon BC-250 → Update Driver → Browse to `output\` → Install → Reboot.
3. **Run tests** (from `output\`):
   - `safe-test.exe` – validates basic IOCTL communication and register access.
   - `deep-test.exe` – performs deeper NBIO/DF/MMHub scans and write tests.
   - `test-wddm.exe` – runs WDDM compatibility tests (some skipped due to stub limitations).
   - `test-driver-check.exe` – additional IOCTL validation (GPU_INFO, FIREWALL, REG_TEST).
   - `test-psp-init.exe` – checks PSP firmware and mailbox status.

### Single Test Execution
To run a specific test after building:
```cmd
output\safe-test.exe
```

## Code Architecture & Structure

### High-Level Layout
- **Kernel-Mode Driver (KMD)**: `atikmdag.sys` (WDM control device)  
  Provides IOCTL interface (`\\.\AMDBC250Dream`) for user‑mode tools to query registers, allocate memory, and manage GPU state.  
  Coexists with Microsoft BasicDisplay (WDDM stub) because `DxgkInitialize` is not exported on Windows 11 26100.
- **PSP Driver**: Separate project (`PspDriver.sys` in companion repo) – handles PSP mailbox and NBIO unlock when operational.
- **User-Mode Driver (UMD)**: Direct3D 9 implementation (`amdbc250_umd_v46.c`) for legacy graphics.
- **Vulkan ICD**: `bc250_vulkan_icd.c` – passes 13/13 official Vulkan loader tests.
- **Test Tools**: User‑mode executables that communicate via IOCTLs to validate hardware access.

### Key Directories
- `src/kmd/` – Kernel‑mode source:
  - `amdbc250_dream_kmd.c` – `DriverEntry`, IRP dispatch, IOCTL handlers, init data.
  - `amdbc250_dream_hw_init.c` – GPU initialization, ring buffers, display, PSP integration.
  - `amdbc250_dream_power.c` – Power/thermal management (stubs).
  - `amdbc250_dream_vm.c` – GPUVM, GART, page table management.
  - `amdbc250_psp_v11.c` – PSP BAR5 mapping, MP0 discovery, ring handling, NBIO unlock attempts.
  - `firmware_data.h` – Embedded PSP firmware (SOS, ASD, TA) binaries.
- `src/umd/` – `amdbc250_umd_v46.c` – D3D9 DDI implementation (~45 functions).
- `src/vulkan/` – Vulkan ICD (`bc250_vulkan_icd.c`, ACO wrapper, shader translator).
- `inc/` – Shared headers:
  - `amdbc250_dream_kmd.h` – KMD structures, IOCTL codes.
  - `amdbc250_dream_hw.h` – Hardware register definitions (BC‑250 corrected offsets).
  - `amdbc250_psp_v11.h` – PSP context and API.
  - `amdbc250_ioctl.h` – IOCTL definitions and input/output structures.
- `test-tools/` – Source and batch scripts for test executables:
  - `safe-test.c`, `deep-test.c`, `test-wddm.c`, `test-psp-init.c`, `test-driver-check.c`.
- `output/` – Build artifacts (signed `.sys`, `.dll`, `.inf`).
- `docs/` – Analysis notes (NBIO firewall, Linux AMDGPU comparison, UEFI tools, BIOS settings).
- `inf/` – Driver INF file (`amdbc250_dream.inf`).

### Critical Hardware Details (BC‑250 / Cyan Skillfish)
- **Register Offsets**: GC registers are shifted relative to Navi10.  
  All driver register accesses use `GC_BASE = 0x1260` (segment 0) and `GC_BASE_SEG1 = 0xA000` (segment 1).  
  Physical BAR5 address: `0xFE800000`.  
  Example: `GRBM_STATUS` is at `0xFE800000 + 0x1260 + 0x2000 = 0xFE803260`.
- **NBIO Firewall**: Blocks direct access to CP/GRBM/SDMA registers at *standard* Navi10 offsets (returns `0xFFFFFFFF`).  
  At corrected offsets (`0x3260+`) these registers are readable/writable (if powered).  
  No known software bypass; PSP must unlock NBIO via mailbox (currently disabled on this SKU).
- **Power/Gating**: GC block likely power‑gated. Writes to `CC_GC_SHADER_ARRAY_CONFIG` and `SPI_PG_ENABLE_STATIC_WGP_MASK` are ignored until SMU powers up the domain via `RequestActiveWgp` (msg 0x18).
- **SMU v11.8**: Mailbox registers at `MP1_BASE = 0x16000` in BAR5:  
  - `C2PMSG_66` (write msg) → `0x16A08`  
  - `C2PMSG_82` (write arg/read result) → `0x16A48`  
  - `C2PMSG_90` (response) → `0x16A68`  
  Protocol: clear response (`0`) → write arg → write msg → poll response (`1=OK`, `0xFF=FAIL`).
- **Memory**: BAR0 (`0xC0000000`) is the VRAM window (UMA, 8 GB GDDR6 shared with CPU).  
  BAR2 (`0xD0000000`) is the doorbell register (2 MB).

### Working Features (as of latest commit)
- IOCTL channel fully functional; user‑mode tools can read/write MMIO registers via corrected offsets.
- `INIT_HARDWARE` maps BAR5 and BAR0.
- Full register read capability (GPU_ID, HDP, GC config, GRBM, CP scratch, SDMA, MMHUB, Data Fabric, NBIO).
- Write capability to MMHUB, GC config, and some GC registers (writes to CC/SPI ignored without SMU power‑up).
- Memory allocation (`ALLOC_VIDMEM`) returns contiguous physical pages.
- Vulkan ICD and D3D9 UMD pass basic adapter/tests.
- PSP proxy driver (`PspDriver.sys`) can open `\\.\AmdBcPsp` for direct PSP MMIO when enabled.
- Build pipeline produces signed `atikmdag.sys` and installs correctly.

### Known Limitations / Ongoing Work
- **NBIO firewall** remains locked; PSP function (01:00.2) is disabled (both BARs disabled, IRQ 255). No PSP firmware released for Cyan Skillfish2.
- **GC power‑up**: SMU communication (TestMessage, GetSmuVersion) needs verification; `RequestActiveWgp` (0x18) to enable WGPs not yet tested.
- **CP firmware loading**: Requires DMA of embedded `firmware_data.h` (PFP/ME/CE/MEC) – not yet implemented.
- **Ring initialization**: GFX/SDMA rings depend on CP firmware and power‑up.
- **VRAM reporting**: Windows reports 16 GB due to UMA configuration; actual GDDR6 is 8 GB.
- **DxgkInitialize** not exported → driver is a WDM control device, not a true WDDM miniport; coexists with BasicDisplay.

## Typical Workflow for Claude
1. **Explore**: Use `Read` on `README.md`, `DEV-GUIDE.md`, and `BUILD-FIX-GUIDE.md` for context.
2. **Locate code**: Use `Glob`/`Grep` to find register definitions (`inc/amdbc250_dream_hw.h`) or IOCTL handlers (`src/kmd/amdbc250_dream_kmd.c`).
3. **Build**: Run `build.bat` (may need to satisfy prerequisites first).
4. **Test**: Execute `output\safe-test.exe` to verify changes.
5. **Iterate**: Edit source, rebuild, retest.

> **Note**: Modifying register access requires understanding the BC‑250 offset correction; always use the provided macros (e.g., `AMDBC250_REG_*`) from `amdbc250_dream_hw.h` rather than raw offsets.

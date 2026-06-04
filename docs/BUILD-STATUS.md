# AMD BC-250 Dream Drivers — Build Status

## Build Environment

| Component | Status | Version |
|-----------|--------|---------|
| **MSVC Compiler** | ✅ | 19.44.35225 (VS 2022 Community) |
| **Windows SDK/WDK** | ✅ | 10.0.26100.0 |
| **Inf2Cat** | ✅ | 10.0.26100.0 |
| **Signtool** | ✅ | 10.0.26100.0 |
| **Test Certificate** | ✅ | CN=AMD-BC250-Signer (Root + TrustedPublisher) |

## Build Method

Direct MSVC compilation via `build.bat` (no WDK build environment needed):
- WDK headers + libraries from Windows Kit 10.0.26100.0
- Kernel mode flags: `/kernel /DAMD64 /DAMDBC250_DREAM_V3`
- Link: `/DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry`

## Source Files Compiled into atikmdag.sys

| File | Purpose | Status |
|------|---------|--------|
| `amdbc250_dream_kmd.c` | DriverEntry, IOCTL dispatch, WDDM callbacks | ✅ |
| `amdbc250_dream_hw_init.c` | GPU init, rings, display, PSP init (Step 9) | ✅ |
| `amdbc250_dream_power.c` | Power/thermal management | ✅ |
| `amdbc250_dream_vm.c` | GPUVM, GART, page tables | ✅ |
| `amdbc250_psp_v11.c` | PSP: GPU BAR5 map, MP0 discovery, NBIO unlock | ✅ |

## Signing

- Signed with `CN=AMD-BC250-Signer` (self-signed test certificate)
- Certificate in: `LocalMachine\Root` + `LocalMachine\TrustedPublisher`
- Both `atikmdag.sys` and `amdbc250_dream.cat` are signed
- Test signing: `bcdedit /set testsigning on`

## Output (498 KB)

`output\atikmdag.sys` — single driver with PSP logic integrated.
No separate `amdbc250_psp.sys` — PSP is compiled INTO the dream driver.

## PSP Integration (new in v4.3)

PSP initialization runs automatically during `DreamV3HwInitialize()` as Step 9:
1. Map GPU BAR5 (0xFE800000)
2. Discover MP0 base by scanning DWORD offsets for SOS alive signal (C2PMSG_81)
3. If SOS alive, create PSP rings and attempt NBIO unlock via 0xC100/0xC180
4. All non-fatal — driver continues if PSP unavailable

Based on Linux amdgpu `psp_v11_0_8.c` analysis — see `docs/LINUX-AMDGPU-ANALYSIS.md`.

## Installation (IMPORTANT: always uninstall first!)

1. `bcdedit /set testsigning on` + reboot (one-time)
2. Build: `build.bat`
3. Device Manager → AMD Radeon BC-250 → **Uninstall device** (check "Delete driver")
4. Reboot
5. Device Manager → Update driver → Browse to `output\` → Select INF
6. Reboot

## Test

```cmd
output\safe-test.exe        # Basic IOCTL tests (no PSP)
output\test-psp-init.exe    # PSP init + SOS detection
```

## Notes

- Old separate `amdbc250_psp.sys` driver removed — PSP fully integrated into dream driver
- PSP firmware is pre-loaded by BIOS (confirmed via Linux analysis)
- No firmware loading needed — only ring setup + NBIO unlock

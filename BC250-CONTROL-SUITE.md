# BC-250 Control Suite

Consolidated, **working** Windows-host control surface for the AMD BC-250
(Cyan Skillfish / RDNA2 mining ASIC, PCI `1002:13FE`).

The BC-250 is a **display-output card that works out of the box on Windows**
(the SOS/PSP firmware configures a default DisplayPort output at boot —
a monitor plugged into the BC-250 shows the Windows login). The host
(Windows driver) is **locked out of everything the SOS owns**, so this
suite is a **hardware control / debug** toolkit, not a compute or
display-driver project.

## What the host CAN do (proven working)

| Capability | Path | Status |
|---|---|---|
| SMU control (freq / voltage / features / sensors) | GPU driver, SMN via NBIO `BAR5+0x38/0x3C` | ✅ works |
| PSP mailbox firmware load (RLC/MEC/ME/PFP/CE) | PSP driver (`\.\AmdBcPsp`) | ✅ works |
| Register read/write proxy (writable aliases) | GPU driver `READ_REG`/`WRITE_REG` | ✅ works |
| Display output | SOS default at boot | ✅ works (host cannot re-program) |

## What the host CANNOT do (SOS-owned, read-only to host)

| Block | Evidence | Why |
|---|---|---|
| GFX ring `CP_RB0_BASE` (0x89E0) | `gfx-ring-ro-test` → READ-ONLY | SOS owns CP |
| KIQ `KIQ_BASE_LO` (0xE060) | writable but `RPTR` stays 0 | CP never fetches |
| DCN timing/OTG (0x6000–0x6024) | `dcn-init-test` → READ-ONLY | SOS owns display |
| `MC_VM_FB_LOCATION` (0x6EE0) | reads 0, writes ignored | no VRAM GPU-VA window |
| Doorbell / HQD control | READ-ONLY | SOS owns |
| `SPI_PG_ENABLE_STATIC_WGP_MASK` (0x5C3C) | READ-ONLY = 0 | **WGP fused off** |

**Conclusion:** no GPU ring submission (GFX / KIQ / compute / 3D) is
possible from Windows. The GFX/DCN "init" sequences described in
external forks (`ps5-win-driver`, `ZEROAESQUERDA/BC250-windowsDriverTest`)
are **false positives** — they write to writable-but-wrong offsets
(e.g. `0xC100`) and rely on a trivial `SCRATCH` handshake that
cannot fail.

## Tools

### `bc250ctl.exe` — unified CLI

```
bc250ctl info                       GPU id, SMU ver, features, freq, active WGP
bc250ctl features                    read + decode enabled SMU features
bc250ctl freq <mhz> [mv]           SAFE set: unforce, force vid(mv), force freq
bc250ctl vid <mv>                     force GFX voltage (mV)
bc250ctl unforce                      release forced freq + vid
bc250ctl psp status                   PSP alive / fwLoaded / ringCreated
bc250ctl psp load <rlc|mec|me|pfp|ce>   load fw via mailbox
bc250ctl reg r <off>                 read BAR5 register
bc250ctl reg w <off> <val>           write BAR5 register
```

Requires both drivers installed and test-signing on:
- GPU driver `atikmdag.sys` → device `\.\AMDBC250DreamV43`
- PSP driver `PspDriver.sys` → device `\.\AmdBcPsp`

Firmware binaries (`cyan_skillfish2_*.bin`) live in `firmware\` and are
copied to `C:\Windows\System32\drivers\bc-250\` by the GPU INF.

### Other proven probes (test-tools\*.c)

- `gfx-ring-ro-test.c` — decisive: GFX ring base is READ-ONLY.
- `gpu-kiq-test.c` — KIQ ring: RPTR never advances.
- `dcn-init-test.c` — DCN engine alive (Pipe3 OTG `0x270D`) but timing RO.
- `display-output-probe.c` — DP PHY present (idle), no monitor detected.
- `bar5-smn-test.c` / `governor-sequence.c` / `smu-monitor.c` — SMU via SMN.
- `psp-mailbox-rlc-test.c` — PSP mailbox firmware load.

## IOCTL surface (GPU driver)

| IOCTL | Code | Purpose |
|---|---|---|
| `IOCTL_AMDBC250_INIT_HARDWARE` | 0x80000B80 | Map BAR5 (flag `NBIO_MAP` = safe, no full init) |
| `IOCTL_AMDBC250_READ_REG` | 0x80000B88 | BAR5 register read (struct `{ULONG Off; ULONG Val;}`) |
| `IOCTL_AMDBC250_WRITE_REG` | 0x80000B8C | BAR5 register write |
| `IOCTL_AMDBC250_GPU_KIQ_TEST` | 0x80000BD0 | KIQ diagnostic (ring alloc + PM4 kick) |
| `IOCTL_AMDBC250_BAR5_READ_PROXY` | 0x900 | raw proxy (used by PSP driver) |
| `IOCTL_AMDBC250_BAR5_WRITE_PROXY` | 0x901 | raw proxy (used by PSP driver) |

PSP driver IOCTLs: `0x22200C` init, `0x222020` status,
`0x222040` boot, `0x222090` load-IP-fw-direct.

## Build

```
test-tools\compile-bc250ctl.bat     # -> output\bc250ctl.exe
build.bat                          # -> output\atikmdag.sys + PspDriver.sys
```

## References

- `gottmoz/BC-250-Windows-graphics-driver` — display stability playbook (512 MB, QuerySegment3, strict child).
- `ZEROAESQUERDA/BC250-windowsDriverTest` — WDDM reference doc (false-positive GFX init).
- `mothenjoyer69/bc250-documentation` — Mesa addrlib `AMDGPU_NAVI10_RANGE` 0x0A→0x8A (Navi10/GFX10.1.x memory scheme).
- Community: Mesa MR 33116, ROCm #6313, RADV `RADV_DEBUG=nocompute`.

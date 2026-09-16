# BC-250 Achievements (verified on hardware)

Date-stamped, build-stamped. Test-expiry rule (~3 days) applies:
re-run before citing as current fact.

## 2026-09-16 — Vulkan ICD full pipeline (driver: atikmdag.sys 139776B signed Valid)

- `vk-minimal-test.exe` EXIT=0: GPU0 AMD BC-250, API 1.3.0.
- Full `vulkaninfoSDK.exe` (SDK 1.4.357.0) EXIT=0: instance ext, 9 layers
  recognizing GPU0, properties/limits/queues/memory(16GB)/features.
- ICD crash bugs fixed (all in `src/vulkan/bc250_vulkan_icd.c`):
  physical-device handle `(1)`→`&g_Device` (AV);
  queue-family `memset 64B`→real 24B structs (heap corruption 0xC0000374);
  `Properties2`/`MemoryProperties2` pNext preserve at offset 16;
  format stubs zeroed/sane (garbage depth=3524901744 crashed consumers);
  `ToolProperties` stub added.
- `vkcube.exe` exit 1 (no crash): `present support=false`, needs WSI stack.
- Registry cleaned: dangling `C:\Windows\System32\amdbc250_icd.json`
  removed; ICD registered via `output\amdbc250_icd.json`.

## 2026-09-16 — Code 52 fixed, driver RUNNING

- `output\atikmdag.sys` was NotSigned after recompile → signed with
  AMD-BC250-Signer (SHA1 34AFF96C), CAT regenerated (Inf2Cat) + signed.
  Both Valid. `sc query atikmdag` = RUNNING. Script: `src/radv-bc250/sign-driver.bat`.
- `smu_state_monitor`: SMU 88.6.0, GFX 1500MHz @ 931mV, WGP=0,
  8 cores @3500MHz, CPU 1206mV, VRAM 16GB (hardcoded).

## 2026-09-16 — RADV winsys backend (`src/radv-bc250/`, commit 9db361c)

- `radeon_winsys` implementation over KMD IOCTLs (BC-250 equivalent of
  Collabora `wddm_submit_command`→D3DKMT): `cs_submit`→SUBMIT_COMMANDS
  (0x80000880), buffer ops→ALLOC/FREE/MAP/UNMAP_VIDMEM, fences→WAIT/SIGNAL.
- Standalone compile BUILD_EXIT=0 (3 .obj). Mesa 26.3.0-devel mainline
  confirmed to have NO WDM backend (only `winsys/amdgpu`); WDDM lives in
  Faith Ekstrand dev MR branch (runs CS2, non-conformant, CPU present).
- ACO: `CHIP_GFX1013` first-class in Mesa; wrapper test ALL PASS
  (real GLSL→SPIR-V via glslangValidator, magic OK, S_ENDPGM, regs sane).
  Real codegen needs Mesa build (meson+LLVM) — future work.

## 2026-09-16 — IOCTL ABI + safety map (installed KMD serves LEGACY ABI)

- ALLOC 0x80000840: in ULONG[3]→out {PA,VA}; VA is kernel-mapped
  (0xFFFF…), user write AVs by design → ICD uses SEND_PM4-inline.
- SUBMIT 0x80000880 (ULONG[4]{lo,hi,size,fence}) **BSODs 0xA**.
  Analysis: `KeSetEvent` on never-initialized `FenceEvent` (top suspect),
  uninitialized `GfxRing.Lock`, OOB ring write. No submit tests until fixed.
- GART/VM/ring 0x1A causes mapped: MC 0x9528/2C/30, FB 0x9520/24,
  ctx0 0xB460, unhalt+fetch-garbage. Fix plan with kill-switches on file.
- APU reframing: unified GDDR6; GART/VM off = CP translates nothing.
  CMOS live: UMA_SIZE=512MB, 1750 timings profile.

## 2026-09-15 — SMU surface fully verified (16/16 PASS)

- `smu-all-msgs-test`: 7 reads + 9 writes with wedge check, all OK.
- SMU writes persist after reboot (Q3 0x8F 4000MHz).
- CPU OC quartet verified: 0x50/0x8B/0x8C/0x8F.
- SRAM Q3 0x28/0x29 confirmed write-only (no host readback).

## Standing blockers

1. WGP/SPI_PG SOS-locked on Windows (EFI route only) — no shader execution.
2. KMD SUBMIT/GART/VM/ring init unsafe (BSOD 0xA/0x1A) — fix planned.
3. WSI stack missing (vkcube present) — after 1.
4. Real ACO build — needs Mesa toolchain.

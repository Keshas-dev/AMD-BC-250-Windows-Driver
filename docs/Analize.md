Now I have all the information needed. Let me compile a comprehensive summary.

Complete GPU Initialization Flow Analysis for AMD BC-250 Windows Driver
1. OVERALL INIT SEQUENCE (TWO ENTRY POINTS)
The driver has two parallel init paths:

Path A: WDDM DDI Init (DreamV3DdiStartDevice in amdbc250_dream_kmd.c:479) Called by dxgkrnl when the adapter is started. This is the primary init path.

Path B: IOCTL Init (IOCTL_AMDBC250_INIT_HARDWARE = 0x80000B80 in amdbc250_dream_kmd.c:3308) Called by user-mode test tools/Vulkan ICD after they open \\.\AMDBC250DreamV43.

Both paths end up calling DreamV3HwInitialize().

2. DDI START DEVICE FLOW
File: C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd\amdbc250_dream_kmd.c, lines 479-678

Step	Lines	What Happens	Status
1. Get device info	502-516	DxgkCbGetDeviceInformation	OK
2. Set PCI IDs	520-541	Hardcoded vendor 0x1002, device 0x13FE	OK
3. Map MMIO BAR	543-606	Iterates resource list, maps first memory resource < 16MB as MmioVirtualBase via MmMapIoSpace	PARTIAL - Heuristic may map wrong BAR
4. Call DreamV3HwInitialize	609	Full hardware init (see section 3)	OK (non-fatal on failure)
5. 40 CU unlock via registry	623-655	If Enable40CU=1, writes AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG(0x3264) = 0xFFE00000, AMDBC250_REG_SPI_PG_ENABLE_STATIC_WGP_MASK(0x34FC) = 0x1F	WRITES SILENTLY IGNORED
6. Create WDM control device	658-678	Creates \\.\AMDBC250DreamV43 for IOCTL access	OK
3. DreamV3HwInitialize — 10 Steps
File: C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd\amdbc250_dream_hw_init.c, lines 65-229

Step 1: Memory Controller Init (lines 76-85)
File: amdbc250_dream_hw_init.c:765-796
Writes AMDBC250_REG_GB_ADDR_CONFIG (0x9800) with 4 pipes, 256B interleave
Writes MC_VM_FB_LOCATION_TOP/BASE (0x524/0x520) from framebuffer resource
✅ Works — reads back OK
Step 2: Golden Register Programming (lines 88-97)
File: amdbc250_dream_hw_init.c:356-390
Copies GB_ADDR_CONFIG_READ → GB_ADDR_CONFIG
Disables compute ring 0 (quirk AMDBC250_QUIRK_BROKEN_COMPUTE_QUEUE)
Enables HDP coherency (HDP_NONSURFACE_INFO = 1)
⚠️ Non-fatal — many real golden regs from gfx_v10_0.c not programmed
Step 3: IH Ring Init (lines 100-109)
File: amdbc250_dream_hw_init.c:492-536
Allocates 256KB contiguous IH ring buffer
Programs IH_RB_BASE_LO (0x3800), IH_RB_CNTL (0x3808)
Initializes read pointer, enables interrupts (IH_CNTL = ENABLE_INTR | RPTR_REARM)
✅ Works
Step 4: GFX Ring Init (lines 112-121)
File: amdbc250_dream_hw_init.c:396-486
Allocates 2MB contiguous ring buffer + 4KB fence page
Halts CP (CP_ME_CNTL = ME_HALT | PFP_HALT)
Programs ring base (LO/HI at 0xC800/0xC804), ring control (0xC808)
Calls DreamV3InitCommandProcessor (see section 4)
Resumes CP (writes 0 to CP_ME_CNTL)
⚠️ Ring programmed but CP firmware NOT loaded — CP cannot process commands
Step 5: SDMA Ring Init (lines 124-133)
File: amdbc250_dream_hw_init.c:542-581
Allocates 512KB contiguous SDMA ring buffer
Programs SDMA0_GFX_RB_BASE_LO/HI (0xE000/0xE004), SDMA0_GFX_RB_CNTL (0xE008)
⚠️ SDMA firmware not loaded, engine not kicked off
Step 6: GART Init (lines 136-145)
File: amdbc250_dream_vm.c:76-125
Allocates 128KB GART page table (16384 entries × 8 bytes)
Programs MC_VM_AGP_BASE/TOP/BOT (0x528/0x52C/0x530)
✅ Works (software table only, hardware not actively used)
Step 7: GPUVM Init (lines 148-157)
File: amdbc250_dream_vm.c:230-267
Initializes VMID bitmap (16 VMIDs, VMID 0 reserved)
Calls DreamV3VmConfigureSystemAperture (not read, assume stub)
Initializes 16 VM context structures
✅ Works (software structures only)
Step 8: Display Init (lines 160-169)
File: amdbc250_dream_hw_init.c:660-724
Enables OTG control (0x6000)
Programs 640x480@60Hz VGA timing or 1920x1080@60Hz
⚠️ OTG registers may not be correct for DCN 2.1 on BC-250
Step 9: PSP Init (lines 172-182)
File: amdbc250_dream_hw_init.c:589-654
Calls Amdbc250PspInit(0) — maps BAR5 at 0xFE800000, discovers MP0 base
Reads C2PMSG_81 to check if SOS is alive
Attempts NBIO unlock via PSP mailbox
Checks for EFI Shell injection fallback
✅ Works — SOS is alive, PSP context initialized
Step 10: GFX Ring Re-init via NBIO/PSP (lines 192-212)
Retries DreamV3HwInitGfxRing if NBIO unlocked or PSP proxy available
⚠️ Redundant with Step 4 — same result
4. CP Initialization (What's Missing)
File: amdbc250_dream_hw_init.c:730-758

The DreamV3InitCommandProcessor function does ONLY a scratch register test:

DreamV3WriteRegister(DevExt, AMDBC250_REG_SCRATCH_REG0, 0xDEADBEEF);
ULONG TestVal = DreamV3ReadRegister(DevExt, AMDBC250_REG_SCRATCH_REG0);
What SHOULD be done (from Linux gfx_v10_0.c):

Component	Firmware File	Status	Registers
PFP (Prefetch Parser)	navi10_pfp.bin	NOT LOADED	CP_PFP_UCODE_ADDR (0xC0A0), CP_PFP_UCODE_DATA (0xC0A4)
ME (Micro Engine)	navi10_me.bin	NOT LOADED	CP_ME_UCODE_ADDR (0xC0B0), CP_ME_UCODE_DATA (0xC0B4)
CE (Constant Engine)	navi10_ce.bin	NOT LOADED	(separate ucode address/data)
MEC (Micro Engine Compute)	navi10_mec.bin	NOT LOADED	CP_MEC_CNTL (0xC0E0)
RLC (Run List Controller)	navi10_rlc.bin	NOT LOADED	RLC ucode + PG registers
RLC SRAM	(ucode)	NOT LOADED	RLC_GPM_UCODE_ADDR/DATA
The CP registers at 0xC000 range are defined in amdbc250_dream_hw.h:202-209 but only CP_ME_CNTL and CP_ME_STATUS are ever used. The firmware upload registers are defined but never written.

5. SMU/Power Init (STUB)
File: C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd\amdbc250_dream_power.c

Function	Lines	What It Does	Status
DreamV3SmuSendMessage	107-130	STUB — returns immediately, does nothing	BROKEN — comment says "BC-250 has NO SMU firmware"
DreamV3SmuInitialize	182-231	Calls stub SMU messages, no real hardware init	BROKEN — GFX/SDMA not powered up
DreamV3SetPowerStateD0	280-313	Software-only state tracking	BROKEN — no real D0 entry
DreamV3UpdateClocks	472-517	Software-only clock tracking	STUB — doesn't actually change clocks
Critical missing SMU messages that affect GC register writes:

SMU_MSG_PowerUpGfx (0x6) — powers up the GC block
SMU_MSG_PowerUpSdma (0x7) — powers up the SDMA engine
SMU_MSG_EnableGfxOff (0x4) — enables GFX power-off (needed for PG)
Since all SMU messages are stubbed, the GC block may never be powered up. This is the most likely reason GC configuration register writes are silently ignored.

6. PSP Init Details
File: C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd\amdbc250_psp.c

Function	Lines	What It Does	Status
Amdbc250PspInit	401-445	Maps BAR5 (0xFE800000, 512KB), discovers MP0 base, checks SOS	✅ Works
Amdbc250PspTryUnlockNbio	511-529	Sends C2PMSG_64=0x20000, writes NBIO sig registers (0xC100, 0xC180)	⚠️ Unsure if effective
PspProxyInit	42-91	Opens \\.\AmdBcPsp, calls PSP_IOCTL_GET_GPU_INFO	✅ Works
Amdbc250PspProxyReadReg	94-111	Reads via PSP driver IOCTL, falls back to direct PSP MMIO	✅ Works
Amdbc250PspProxyWriteReg	114-133	Writes via REG_PROG (ring) or direct PSP MMIO	⚠️ Ring write may be silently ignored
PSP Proxy state after init:

g_PspProxyAvailable = TRUE (if PSP driver is running)
g_GpcomRingPa = GPCOM ring physical address from PSP_GET_GPU_INFO
g_GpcomRingAvailable = TRUE if ring PA != 0
g_GpcomRingVa = mapped GPCOM ring
7. Register Read/Write Path Analysis
File: C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc\amdbc250_dream_kmd.h:602-626

DreamV3ReadRegister(DevExt, Offset):
    READ_REGISTER_ULONG(MmioVirtualBase + Offset)

DreamV3WriteRegister(DevExt, Offset, Value):
    WRITE_REGISTER_ULONG(MmioVirtualBase + Offset, Value)
The MmioVirtualBase is the physical MMIO BAR mapped via MmMapIoSpace with MmNonCached (uncached), so writes should go directly to the hardware.

GC register offsets used for 40 CU unlock:

AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG = 0x1260 + 0x2004 = 0x3264 (defined in amdbc250_dream_kmd.h:957)
AMDBC250_REG_SPI_PG_ENABLE_STATIC_WGP_MASK = 0x1260 + 0x229C = 0x34FC (defined in amdbc250_dream_kmd.h:960)
These are the corrected BC-250 offsets (not Navi10 defaults).

8. Root Cause Analysis: Why 40 CU Writes Are Ignored
Likelihood ranking of why writes to 0x3264 and 0x34FC don't stick:

Cause	Likelihood	Explanation
1. GC block not powered (SMU stub)	HIGH	SMU PowerUpGfx is never sent — GC block may be in reset/power-gated state where writes are dropped
2. RLC not initialized	HIGH	RLC controls GC power gating; without RLC ucode loading and init, the GC sub-blocks remain unpowered
3. CP firmware not loaded	MEDIUM	CP microcode (PFP, ME, CE) must be loaded before the CP can process register writes that go through the CP path. However, these are direct MMIO registers, not CP-mediated.
4. Write-once hardware fuses	MEDIUM	The CC_GC_SHADER_ARRAY_CONFIG may be fused at manufacturing. Only test: check if Linux unlock actually persists across reboots
5. Register needs SMN path	LOW-MEDIUM	IOCTL 0x80000BC4 (SMN_ACCESS) exists — some GC registers require SMN index/data access via 0xD00/0xD04, not direct BAR5 offset
6. NBIO write firewall	LOW	Reads work at corrected offsets, writes might be separately firewalled. But confirmed on Linux that devmem writes work at these offsets
7. Wrong MMIO BAR mapped	LOW	If reads work and return valid data, the correct BAR is mapped
8. HDP coherency not flushed	LOW	DreamV3HdpFlush is called but writes are MMIO uncached, so flush shouldn't matter for register writes
Most probable root cause: GC block is in reset/power-gated state because SMU communication is completely stubbed out.

9. Summary: What's Initialized vs What's Missing
Subsystem	Initialized?	Details
MMIO Mapping	PARTIAL	Maps resource BAR (<16MB) — assumes correct BAR. Registers read OK at corrected offsets.
Memory Controller	YES	GB_ADDR_CONFIG + FB location
Golden Registers	MINIMAL	Only 3 regs programmed; many GFX10 golden regs missing
IH Ring	YES	256KB ring allocated and enabled
GFX Ring (buffer)	YES	2MB ring allocated, base/control registers programmed
GFX Ring (CP firmware)	NO	PFP, ME, CE, MEC ucode NOT loaded. CP is halted then immediately resumed with no firmware.
CP Scratch Test	YES	Verifies CP is alive by writing/reading scratch reg
SDMA Ring (buffer)	YES	512KB ring allocated, base registered
SDMA Ring (firmware)	NO	SDMA firmware NOT loaded, engine not kicked off
Compute Rings	NO	Disabled by quirk (AMDBC250_QUIRK_BROKEN_COMPUTE_QUEUE)
KIQ Ring	NO	Explicitly marked as STATUS_NOT_IMPLEMENTED in code
RLC	NO	RLC power management NOT initialized at all
SMU Communication	STUB	All SMU messages return success without actually sending. GFX/SDMA never powered up.
PSP Proxy	YES	Opens PSP driver, checks SOS, GPCOM ring info retrieved
NBIO Unlock	ATTEMPTED	PSP mailbox + NBIO signature register writes attempted
GART	YES	Software table allocated and base programmed
GPUVM	YES	Software context structures initialized
Display (DCN 2.1)	YES	OTG enabled, VGA/1080p timing programmed
Power Management	STUB	D0/D3 transitions, clock scaling, thermal monitoring — all software-only
40 CU Unlock Registers	WRITTEN BUT IGNORED	Both registry and IOCTL paths write registers but values don't persist
10. What's Needed to Fix 40 CU Writes
To make CC_GC_SHADER_ARRAY_CONFIG and SPI_PG_ENABLE_STATIC_WGP_MASK writable, the following must happen:

Power up the GC block — un-stub DreamV3SmuSendMessage and actually send SMU_MSG_PowerUpGfx via the SMU mailbox (C2PMSG_33/66 registers at 0x16284/0x16104), OR power up via PSP mailbox TOS protocol
Load CP firmware — upload PFP, ME, and CE ucode blobs to the CP via CP_PFP_UCODE_ADDR/DATA and CP_ME_UCODE_ADDR/DATA registers, then de-assert CP halt and verify CP is operational
Initialize RLC — load RLC firmware to manage GC power gating states, or alternatively disable RLC power gating by writing RLC_PG_ALWAYS_ON_WGP_MASK (0x3D64 = 0x1260 + 0x2B04)
Try SMN path as fallback — if direct MMIO writes continue to fail, try writing through SMN index/data registers (IOCTL 0x80000BC4) using SMN address = BAR5_offset >> 2
Verify with Linux test — before investing in firmware loading, test whether devmem 0xFE803264 0xFFE00000 followed by devmem 0xFE803264 returns the written value. If Linux writes also don't stick, the registers are hardware-fused and no driver init will fix it.
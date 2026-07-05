Read ALL .md files in the GPU driver repository at C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\docs\ and also the AGENTS.md at the root. For each file, extract:
1. Key hardware facts about BC-250
2. Register offsets and their status (writable, read-only, dead)
3. What firmware loading methods were tried and results
4. What engine status was found (GFX, compute, display)
5. All blockers and dead ends encountered
6. Everything that DOES work
7. Linux comparison points mentioned

Return all findings in a structured format, organized by topic.
The user wants me to read all .md files in the docs/ directory and the AGENTS.md file, then extract structured information from them. Let me start by finding all the .md files and reading them.

Let me now read all the .md files in the docs/ directory to extract the structured information the user is requesting.

Now let me read all the docs/*.md files in parallel:

Now let me read the remaining .md files that I haven't read yet.

Now let me read the remaining .md files.

Now I have all the files. Let me compile a comprehensive structured summary from all docs and AGENTS.md. This is a massive amount of data. I need to organize it by the user's requested categories:

Key hardware facts about BC-250
Register offsets and their status (writable, read-only, dead)
What firmware loading methods were tried and results
What engine status was found (GFX, compute, display)
All blockers and dead ends encountered
Everything that DOES work
Linux comparison points mentioned
Let me compile this.

Structured Findings from All Documentation
1. KEY HARDWARE FACTS ABOUT BC-250
Aspect	Detail
Codename	Cyan Skillfish
Architecture	RDNA 1.5 (GFX10.1.3) — NOT GCN, NOT standard RDNA2
PCI ID	1002:13FE (rev c1)
GPU IP	GC 10.1.3, NBIO 2.1.1, MP0/PSP 11.0.8, MMHUB 2.0.3, SDMA 5.0.1, DF 3.5.0, THM 11.0.1, SMUIO 11.0.8
CUs	24 stock, 40 unlockable via CC_GC_SHADER_ARRAY_CONFIG + SPI_PG_ENABLE_STATIC_WGP_MASK
VRAM	512MB (BIOS P5.00) — NOT 16GB as driver assumes; Linux detects 256M/512M
Memory	16GB GDDR6 soldered, 256-bit bus, ~448 GB/s
CPU	6-core / 12-thread Zen 2
BAR5	0xFE800000 (512KB MMIO register space)
PSP BAR0	0xFD600000 (256KB)
GC_BASE	0x1260 — all GC registers shifted by this offset
GC Seg1	0xA000 — secondary segment for IC_BASE, HYP_UCODE, etc.
NBIO	Does NOT block GC registers at corrected offsets (0x3200+) — only blocks native 0xC000-0xCFFF range
THM	0x8000 (NOT 0x16600 from Linux headers)
SMU	v11.8, MP1_BASE = 0x16000
VCN	Permanently blocked by Sony firmware lock
Display	DCN 2.0.1/2.1, DisplayPort 1.4 (4K@120 / 8K@60), 2nd DP unpopulated
cg_flags=0, pg_flags=0	No clock gating / power gating
external_rev_id = rev_id + 0x82	
VBIOS	ATOM BIOS 113-AMDRBN-003 (fetched from ACPI VFCT on Linux)
BIOS version	P5.00 (BC250_5.00_clv.bin)
Firmware versions (Linux)	ME 0x63, PFP 0x94, CE 0x25, MEC 0x90, RLC 0x0d, SDMA0 0x34, SMC 88.7.1
Original purpose	Cryptocurrency mining accelerator (BC = blockchain); uses leftover PS5 silicon
VRAM allocation	Set in BIOS; 12 GB selectable on BIOS 3.00+
2. REGISTER OFFSETS & STATUS (verified by probe)
Confirmed Working Registers (accessible at GC_BASE-shifted offsets)
Register	Offset	Access	Value	Notes
GPU_ID	0x0000	R	0x9FFF9700	Chip identification
GRBM_STATUS	0x3260 (0x1260+0x2000)	R	0x00000000	GPU idle
CC_GC_SHADER_ARRAY_CONFIG	0x3264 (0x1260+0x2004)	R	0x00000000	Fused (0 CUs) — writes silently ignored
GRBM_SOFT_RESET	0x326C (0x1260+0x200C)	RO	0x00000000	Cannot reset GC
CP_SCRATCH_REG0	0x32D4 (0x1260+0x2074)	RW	0x4D585042 ("MDPX") — CP ALIVE; bit31 is W1C/hardware-controlled	
SPI_PG_ENABLE_STATIC_WGP_MASK	0x34FC (0x1260+0x229C)	RW	0x00002000	WGP5 only; WRITABLE but writes don't enable without SMU
RLC_PG_ALWAYS_ON_WGP_MASK	0x3D64	R	0x00000000	
CP_RING0_CNTL	0xDA68	Wr	0x0→0x1	Bits constrained
CP_RING0_RPTR	0xDA6C	Wr	0x01200000	Bit 24 permanently set
CP_RING0_WPTR	0xDA78	Wr	0x00100010	FULLY WRITABLE ✅
COMPUTE_RING0_BASE_HI	0xDB64	W		
COMPUTE_RING0_CNTL	0xDB68	W		
COMPUTE_RING0_RPTR	0xDB6C	W		
COMPUTE_RING0_WPTR	0xDB78	W		
KIQ_BASE_LO	0xE060	W		ONLY writable BASE register
KIQ_RPTR	0xE06C	W		
KIQ_WPTR	0xE078	W		
CP_MQD_BASE_ADDR	0x9104	W		Write-back verified
CP_HQD_ACTIVE	0x910C	W		ACKs (reads 1 after write)
PGM_LO	0x8110	W		WRITABLE
PGM_HI	0x8114	W		WRITABLE
GCVM_CONTEXT0_CNTL	0x0B460	RW	0x010CA88D	✅ ALIVE
GCVM_CONTEXT0_PT_BASE_LO	0x0B608	W		
GCVM_CONTEXT0_PT_BASE_HI	0x0B60C	W		
GCVM_L2_CNTL	0x0B360	RW	0x013C67B8	
MC_VM_FB_LOCATION_TOP	0x0524	W		
MC_VM_AGP_BASE	0x0528	W		
GB_ADDR_CONFIG	0x9800	RW		4 pipes, 256B interleave
THM_CTRL	0x8000	RW	0x18	Writable
THM_CURRENT_TEMP	0x8008	R	varies	Temperature sensor
NBIO_ID	0xC100	R	0xFEDCBAEF	
Fence/Doorbell block	0x3AD8-0x3AEC	R	0x02A8xxxx	Doorbell aperture addresses
Dead / Read-Only / Blocked Registers
Register	Offset	Status	Notes
CP_RING0_BASE_LO	0xDA60	READ-ONLY = 0	Cannot set ring buffer address — PRIMARY BLOCKER
CP_RING0_BASE_HI	0xDA64	RO = 0	
COMPUTE_RING0_BASE_LO	0xDB60	RO = 0	
KIQ_CNTL	0xE068	RO = 0	Cannot enable ring
KIQ_SIZE	0xE068	Hardwired to 0	
CP_ME_CNTL (native)	0xC060	R=0, NBIO blocks writes	Cannot halt/resume CP
CP_MEC_CNTL (native)	0xC0E0	R=0, NBIO blocks writes	
CP_PFP_UCODE_ADDR/DATA	0xC0A0/0xC0A4	R, NBIO blocks writes	Cannot load firmware
CP_ME_UCODE_ADDR/DATA	0xC0B0/0xC0B4	R, NBIO blocks writes	
DIM_X	0x80E4	READ-ONLY (shadow)	
DIM_Y	0x80E8	DEAD (0xFFFFFFFF)	
DIM_Z	0x80EC	DEAD (0xFFFFFFFF)	
NUM_THREAD_X/Y/Z	0x80FC-0x8104	READ-ONLY (all 0)	
PGM_RSRC1/RSRC2	0x8128-0x812C	DEAD (0xFFFFFFFF)	
CP_MQD_BASE_ADDR_HI	0x9108	RO = 0xFF11EFE0	
GRBM_GFX_CNTL	0x2022 / 0x4968	DEAD	BC-250 doesn't have this
0xDC60	0xDC60	Cycling FIFO	Debug counter, not dispatch
HDP	0x0F20	RO/0xFFFFFFFF	NBIO blocks or unmapped
SMU C2PMSG	0x16A08+	All 0	SMU firmware not responding
CLK	0x0D00-0x0DFF	NBIO	Always blocked on PS5 derivatives
UVD	(VCN)	Sony FW block	
PSP C2PMSG (from host)	0x16000+	Blocked	Need PSP driver
DISPATCH_INITIATOR	0x80E0	W1C trigger	Sets consumed but no execution
Corrected COMPUTE Register Map (vs old wrong offsets)
Register	Old (hw.h)	CORRECT
DISPATCH_INITIATOR	0xDC60	0x80E0
DIM_X/Y/Z	0xDC64-0xDC6C	0x80E4-0x80EC
START_X/Y/Z	0xDC68	0x80F0-0x80F8
NUM_THREAD_X/Y/Z	0xDC6C	0x80FC-0x8104
PGM_LO/HI	0xDC70	0x8110-0x8114
CP_MQD_BASE_ADDR	0xDAB8	0x9104
CP_HQD_ACTIVE	0xDAC0	0x910C
CP_HQD_VMID	0xDAC4	0x9110
CP_HQD_PQ_BASE	0xDAD8	0x9124
CP_HQD_PQ_CONTROL	0xDAFC	0x9148
CP_HQD_PQ_WPTR_LO	0xDB90	0x91DC
3. FIRMWARE LOADING METHODS TRIED & RESULTS
Method	Target	Result	Notes
PSP Mailbox LOAD_IP_FW (0x06)	RLC (fwType=8)	✅ Status=0 ✅ C2Pmsg81=0	BREAKTHROUGH — SOS supports GFX_CMD_ID_LOAD_IP_FW
PSP Mailbox LOAD_IP_FW (0x06)	MEC (fwType=4)	✅ Status=0 ✅ C2Pmsg81=0	Loads successfully
PSP Mailbox LOAD_IP_FW	All types (ME=1, PFP=2, CE=3, MEC=4, MEC2=5, RLC=8, SDMA0=9, SDMA1=10)	✅ All supported	
Direct BAR5 MMIO LOAD_CP_FW	ME/PFP	❌ NBIO blocks writes to 0xC0A0-0xC0B4	Native firmware upload registers are NBIO-blocked
PSP TOS Ring Protocol	Generic PM4	❌ ringCreated=0	TOS ring protocol not implemented in SOS
PSP GPU PM4 Submit (via IOCTL)	KIQ ring	✅ IOCTL works after METHOD_BUFFERED fix	PM4 written to KIQ ring but MEC doesn't process
PSP BOOT_SEQUENCE (SYSDRV+SOS)	PSP SOS	SOS already loaded	C2PMSG_81 = 0xF0000010 (bit31=SOS alive)
KIQ_BIOS_RING_SUBMIT	KIQ	❌ KIQ_BASE=0, KIQ_SIZE=0	Hardwired to 0
Firmware files available:

cyan_skillfish2_me.bin (263KB, ver 0x63)
cyan_skillfish2_pfp.bin (263KB, ver 0x94)
cyan_skillfish2_ce.bin (263KB)
cyan_skillfish2_mec.bin (268KB)
cyan_skillfish2_rlc.bin (25KB)
cyan_skillfish2_sdma.bin (33KB)
cyan_skillfish2_sos.bin — PSP Secure OS
Sysdrv.bin, Sos.bin, Smu.bin (installed via PSP INF to C:\Windows\System32\drivers\bc-250\)
4. ENGINE STATUS FOUND
GFX Engine
CP is ALIVE: Scratch register = 0x4D585042 ("MDPX")
ME halted initially (ME_CNTL = 0xFFFBD9FB) → Unhalted successfully (write 0 → reads 0)
GFX Ring: WPTR writable ✅, RPTR does NOT advance on kick ❌
BASE_LO: Read-only (0) — cannot set ring buffer address ❌
GRBM_STATUS: 0 → no engine activity even after unhalt
Verdict: Engine registers respond to MMIO but the CP/MEC behind them never processes
Compute Engine
MEC firmware loaded via PSP mailbox ✅ Status=0
DISPATCH_INITIATOR: W1C trigger — sets consumed but no execution
PGM_LO/HI: Writable (can set shader address)
DIM_X/Y/Z, NUM_THREAD, PGM_RSRC1/2: Dead or read-only shadows
COMPUTE registers: All shadows (DIM_X=RO, DIM_Y/Z=0xFFFFFFFF)
8 COMPUTE rings on Linux — but on Windows, all COMPUTE_BASE_LO registers are read-only 0
Verdict: Compute engine not processing — consistent with missing SMU/RLC init
KIQ Engine
KIQ_BASE_LO: ONLY writable BASE register ✅
KIQ_CNTL: Read-only (0) ❌
KIQ_SIZE: Hardwired to 0 ❌
Verdict: Cannot set ring buffer properly; KIQ path non-functional
Display Engine
DCN 2.0.1/2.1: OTG registers present (0x6000+)
On Linux: Display Core initialized, 4K HDMI works
On Windows: WDDM path stubbed (DxgkInitialize not exported on Win11 26100)
Verdict: Potentially functional but untested — WDM driver with BasicDisplay coexistence
SDMA Engine
Registers at 0xE000+ range
SDMA firmware not loaded (via PSP mailbox would work)
Verdict: Untested after PSP mailbox fix
SMU Engine
ALL SMU C2PMSG registers read 0 — SMU firmware NOT running
SMU_WAKE times out
No response to TestMessage (0x1) or GetSmuVersion (0x2)
PRIMARY BLOCKER for all compute/GFX engine activity
5. ALL BLOCKERS & DEAD ENDS
#	Blocker	Category	Details	Status
1	SMU firmware not running	Power	SMU C2PMSG registers all 0; no clock/power to GPU blocks	UNRESOLVED — PRIMARY BLOCKER
2	GFX Ring BASE_LO read-only	Ring Init	0xDA60 returns 0, writes silently ignored	HARDWARE FUSED
3	KIQ_CNTL read-only	Ring Init	0xE068 returns 0, cannot enable ring	HARDWARE FUSED
4	KIQ_SIZE hardwired to 0	Ring Init	Ring buffer size can't be set	HARDWARE FUSED
5	NBIO blocks 0xC000-0xCFFF writes	Register Access	CP_ME_CNTL, CP_MEC_CNTL, ucode upload registers all blocked	FIRMWARE (needs PSP SOS)
6	CP firmware upload via NBIO blocked	Firmware	Native ucode_addr/data registers at 0xC0A0-0xC0B4 blocked	NBIO FIREWALL
7	GRBM_GFX_CNTL dead on BC-250	Engine Select	Neither 0x2022 nor 0x4968 works; GRBM_GFX_INDEX (0x34D0) used instead	HARDWARE VARIANT
8	COMPUTE DIM_Y/Z dead (0xFFFFFFFF)	Compute	Standard RDNA2 compute registers return 0xFFFFFFFF	HARDWARE FUSED
9	RPTR does not advance on WPTR kick	Ring Processing	WPTR writes work but RPTR stays at 0x01200000	ENGINE NOT PROCESSING
10	DISPATCH_INITIATOR doesn't execute	Compute	W1C trigger consumes but no shader execution	ENGINE NOT PROCESSING
11	PSP TOS ring protocol not supported	PSP	ringCreated=0 — SOS doesn't implement ring protocol	SOS FIRMWARE LIMIT
12	DxgkInitialize not exported on Win11 26100	WDDM	WDDM miniport path dead; fallback to WDM	PLATFORM LIMIT
13	VCN firmware locked by Sony	Video	Hardware video encode/decode permanently blocked	SONY FIRMWARE LOCK
14	GCVM_CONTEXT0_CNTL wrong offset initially	VM	Wrong formula led to 0xFFFFFFFF reads; corrected to 0x0B460	RESOLVED
15	PTE flags missing SYSTEM bit	VM	0x61 → 0x63 (added SYSTEM bit)	RESOLVED
16	PSP proxy used wrong BAR mapping	Architecture	PSP proxy mapped wrong physical address for GPU regs	RESOLVED (bypassed)
17	METHOD_BUFFERED buffer sharing	PSP IOCTL	RtlZeroMemory cleared req->CommandCount in same buffer	RESOLVED
18	NBIO unlock writes through GPU BAR5	PSP	Writes silently fail; corrected to use PSP BAR0	RESOLVED
6. EVERYTHING THAT DOES WORK
Hardware Access
✅ BAR5 MMIO mapping (0xFE800000, 512KB via MmMapIoSpace)
✅ All GC_BASE-shifted register reads at corrected offsets (GRBM, CC, SPI, Scratch, KIQ, GCVM, MMHUB, THM, NBIO)
✅ Register writes to writable registers (WPTR, KIQ_BASE_LO, Scratch, GCVM, MMHUB, THM, SPI)
✅ GPU_ID read (0x9FFF9700 — chip identification)
✅ CP Scratch test (CP is alive, returns "MDPX")
✅ THM temperature sensing (0x8008)
✅ ME unhalt (write 0 to 0x4A74 via PSP proxy)
PSP / Firmware
✅ PSP SOS alive (C2PMSG_81 = 0xF0000010)
✅ PSP mailbox firmware loading for ALL GPU firmware types (ME, PFP, CE, MEC, MEC2, RLC, SDMA0, SDMA1)
✅ PSP driver IOCTL communication (read/write proxy, INIT_HW, GET_GPU_INFO)
✅ PSP INF auto-installs firmware (Sysdrv.bin, Sos.bin, Smu.bin to C:\Windows\System32\drivers\bc-250\)
✅ NBIO unlock status detection (GRBM_STATUS reads 0, not 0xFFFFFFFF)
GPU Driver (SW)
✅ WDM IOCTL device (\\.\AMDBC250DreamV43)
✅ IOCTL handlers: INIT_HARDWARE, READ_REG, WRITE_REG, GET_CAPS, GET_VRAM_INFO, ALLOC_VIDMEM, LOAD_CP_FW, PM4_SUBMIT, etc.
✅ GCVM page tables (4-level, identity mapping, writable registers)
✅ GCVM context init (16 VMIDs, system aperture)
✅ GART software table (16K entries)
✅ IH ring setup (256KB, base+control+RPTR)
✅ HDP coherency flush
✅ PM4 packet encoding (NOP, WRITE_DATA, DISPATCH_DIRECT, EVENT_WRITE_EOP, etc.)
✅ Driver build + sign pipeline (build.bat outputs signed atikmdag.sys, amdbc250umd64.dll)
✅ Vulkan ICD (13/13 tests pass)
✅ D3D9 UMD (45+ DDI functions)
✅ PSP driver build + sign (output\PspDriver.sys, output\PspDriver.inf, output\PspDriver.cat)
Test Tools
✅ safe-test.exe, deep-test.exe, test-wddm.exe, psp-status-test.exe, toc-load-test.exe, psp-mailbox-rlc-test.exe, gfx-ring-test.exe, me-unhalt-test.exe, correct-compute-test.exe, etc.
7. LINUX COMPARISON POINTS
Linux Success (dmesg from CachyOS)
Capability	Linux Status	Windows Status
GFX ring	✅ Created: ring gfx_0.0.0 uses VM inv eng 0	❌ BASE_LO RO, RPTR doesn't advance
8 COMPUTE rings	✅ Created: ring comp_1.X.Y	❌ Disabled by quirk; DIM_Y/Z dead
KIQ ring	✅ Created: ring kiq_0.2.1.0	❌ KIQ_CNTL RO, KIQ_SIZE=0
SDMA rings	✅ 2 rings created	❌ Untested with firmware
24 CUs detected	✅ active_cu_number 24	❌ CC_GC_SHADER_ARRAY_CONFIG=0
SMU initialized	✅ SMU is initialized successfully!	❌ ALL C2PMSG registers read 0
VBIOS access	✅ Fetched from ACPI VFCT	❌ No VBIOS access
Display Core	✅ DCN 2.0.1 initialized	❌ Stubbed (WDDM not functional)
Firmware loaded	✅ ALL types (ME/PFP/CE/MEC/RLC/SDMA/SMC)	✅ MEC+RLC via PSP mailbox (partial)
VRAM	256M/512M detected	16384 MB hardcoded (wrong)
PCIE GART	512M enabled at phys 0x00000000	❌ Not set up
PSP TMR	4MB at 0xF41F800000	❓ Not configured
Key Linux Architecture Differences
Linux uses CP_HQD_ registers for KIQ* (MQD model), NOT KIQ_BASE/CNTL/WPTR at 0xE060+ — our KIQ registers are non-standard GFX10
Linux uses mmGRBM_GFX_CNTL (0x0dc2) for ME/PIPE/QUEUE select, NOT GRBM_GFX_INDEX (0x34D0) — we use the wrong register
Linux skips PSP firmware loading entirely for BC-250 (psp_v11_0_8_funcs has NULL for all bootloader functions)
Linux uses SOC15_REG_OFFSET(GC, 0, mmREG) = GC_BASE(0x1260) + mmREG*4 — our verified offsets match this formula for IC_BASE/HYP_UCODE but NOT for GCVM registers (different base segment)
cg_flags=0, pg_flags=0 — Linux confirms no clock/power gating on BC-250
RADV_DEBUG=nocompute is a USERSPACE (Vulkan/Mesa) workaround, NOT a kernel/hardware limitation — Linux kernel creates compute rings successfully
Why Windows Fails Where Linux Succeeds
SMU not initialized → No clock/power to GFX/CP blocks → PRIMARY BLOCKER
No VBIOS load → Missing power-up init sequences
RLC firmware not loaded via PSP → RLC controls engine queue scheduling (loaded via mailbox on Linux)
Ring creation incomplete → BASE_LO registers RO; need HQD/MQD model
KIQ_BASE/KIQ_SIZE hardwired to 0 → Ring buffer address can't be programmed through KIQ path
No SMU firmware file (cyan_skillfish2_smc.bin) — we don't have the SMC firmware blob
No PCIE GART setup at physical 0x00000000
VRAM size mismatch — driver assumes 16GB, hardware has 512MB visible
Linux PSP Boot Sequence (for Windows reference)
1. BIOS loads VBIOS from ACPI VFCT
2. amdgpu maps BAR5 at 0xFE800000
3. Reserved 4MB TMR at 0xF41F800000
4. PSP firmware loaded (optional — NULL in funcs for BC-250)
5. SMU initialized via PSP
6. Display Core DCN 2.0.1 initialized
7. All firmware loaded via PSP (ME, PFP, CE, MEC, RLC, SDMA, SMC)
8. Rings created with VM inv eng assignment
9. GPU ready for compute/graphics
NBIO Firewall — Corrected Understanding
NBIO DOES NOT block GC registers — all GC_BASE-shifted offsets at 0x3200+ are fully accessible
NBIO ONLY blocks native 0xC000-0xCFFF range — writes silently ignored, reads return 0
GC_BASE-shifted aliases bypass NBIO — 0xDA60+ (rings), 0x4A74 (ME_CNTL), 0x4B14 (MEC_CNTL) all bypass
BUT: NBIO may still block the engine execution path (reading ring buffers from VRAM) — this is why RPTR never advances despite WPTR being writable
NBIO unlock requires PSP SOS firmware to configure internal address translation tables

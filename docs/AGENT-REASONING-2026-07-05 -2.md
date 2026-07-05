Read the AGENTS.md file at C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\AGENTS.md. Extract all key information about:
1. PSP driver architecture and IOCTLs
2. Firmware loading methods (mailbox, TOC, ring)
3. SMU firmware loading progress and results
4. SOS firmware status
5. KIQ status
6. All known bugs (fixed and unfixed)
7. Build and install process
8. What works and what doesn't

Return all findings in a structured format organized by topic.
The user wants me to read the AGENTS.md file from the PSP Windows Driver repo and extract structured information. Let me do that.

Let me extract and organize all the key information from this comprehensive AGENTS.md file for the PSP driver.

Structured Summary: PSP Windows Driver AGENTS.md
1. PSP Driver Architecture & IOCTLs
Aspect	Detail
Type	WDM native NT driver (NOT KMDF — 0x7e if WDF linked)
Target	PCI VEN_1022&DEV_143E (AMD PSP on BC-250/PS5 Oberon)
Device	\Device\AmdBcPsp, symlink \DosDevices\AmdBcPsp
IOCTL	METHOD_BUFFERED
User-mode	\\.\AmdBcPsp via DeviceIoControl
MMIO	Maps GPU BAR5 (0xFE800000), not PSP BAR0; C2PMSG mailbox lives in BAR5
Source files	src/driver/PspDriver.c (1052 lines, IOCTL dispatch/INIT_HW/BOOT_SEQ/proxy), PspCore.c (mailbox/firmware/GPU proxy/TMR), PspKiq.c (KIQ ring+GPU HQD programming), PspSmu.c (SMU wake), src/test/test-psp-driver.c (931 lines, CLI test tool), inc/PspIoctl.h (shared IOCTL codes+structs), inc/firmware_data.h (embedded SOS+SYSDRV), inf/PspDriver.inf
Key IOCTLs:

IOCTL	Code	Purpose
PSP_READ_REG	0x800	Read unlocked registers
PSP_WRITE_REG	0x801	Write unlocked registers
PSP_INIT_HW	0x803	Map BAR5 MMIO
PSP_NBIO_UNLOCK	0x804	Write NBIO sigs
PSP_SEND_CMD	0x805	Raw mailbox command
PSP_BOOT_SEQUENCE	0x810	Automated FW boot
PSP_GET_GPU_INFO	0x815	Bridge status snapshot
PSP_LOAD_TOC (SMU)	0x820	Load SMU via TOC
PSP_SMU_WAKE	0x821	Wake SMU
PSP_GPU_PM4_SUBMIT	varies	PM4 commands via GPU
PSP_KIQ_SUBMIT	varies	KIQ ring commands
PSP_LOAD_IP_FW_DIRECT	varies	Direct firmware load via mailbox
INIT sequence: INIT_HW → BOOT_SEQ → NBIO_UNLOCK → GET_GPU_INFO → verify C2PMSG_81=0xF0000010

2. Firmware Loading Methods
A. Mailbox (working, confirmed):

Commands: CMD 0x4 (SYSDRV), CMD 0x8 (SOS) — both work
Protocol: Write PA to C2PMSG_36/37 → Write cmd to C2PMSG_35 → Poll C2PMSG_35 until 0 → DO NOT touch C2PMSG_81
Verified against Linux psp_v11_0_8.c — Linux uses same C2PMSG_35 poll method
SOS is pre-loaded by BIOS; BOOT_SEQUENCE is redundant but harmless
B. GPCOM Ring (NOT supported by SOS firmware):

C2PMSG_64 bit 31 (MBOX_TOS_READY_FLAG) never sets even after 60s poll
Ring addr/size writes (C2PMSG_69/70/71) silently rejected
Linux psp_v11_0_8 has same limitation — ring protocol not supported by BC-250 SOS
No TMR init, no GPU firmware loading via ring, no register programming via ring
C. TOC (SMU via GFX ring command buffer, IOCTL 0x820 — implemented but NOT tested):

Uses GFX_CMD_ID_LOAD_TOC (0x20) with fw_type=7 (PSP_SMC)
Builds TOC: {id=0, entry_count=1, total_size=28} + {fw_type=7, fw_size, fw_pa}
Sends via ring command buffer, polls C2PMSG_35 for completion
toc-load-test.exe exists but needs reboot to test
D. PSP Mailbox for GPU FW (working):

Direct firmware load via mailbox works for ALL types: ME=1, PFP=2, CE=3, MEC=4, MEC2=5, RLC=8, SDMA0=9, SDMA1=10
MEC firmware loaded successfully (cyan_skillfish2_mec.bin, version 0x90)
RLC firmware loaded successfully
Status=0x00000000 for all firmware types
3. SMU Firmware Loading Progress
Status	Detail
SMU firmware exists	cyan_skillfish2_smc.bin (267,970 bytes)
SMU registers	All 0 — C2PMSG_66/82/90 dead after all attempts
PspSendSmcBoot	Implemented but not called during boot (needs GPU BAR5 mapped)
IOCTL_PSP_LOAD_TOC (0x820)	Implemented but not tested (needs driver reinstall + reboot)
IOCTL_PSP_SMU_WAKE (0x821)	Implemented but SMU not responsive
SMU wake timeout	SMU C2PMSG registers all 0
Root cause	Without SMU, GFX/CP/SDMA blocks have no clock/power. This is the PRIMARY BLOCKER for compute/GFX
Linux	SMU initialized successfully via SMC v88.7.1 firmware — SMU is initialized successfully! in dmesg
SMU SPI address: 0x8FEE00 (matches BIOS SYSDRV type 8). SMU memory region from bc250-collective:

SMU command at 0x015400F0, argument at 0x015400F4, trigger at 0x015400F8
SMU NBIF/IOMMU at 0x0154002C, SMU NBIF control at 0x0154001C
4. SOS Firmware Status
Aspect	Detail
C2PMSG_81 value	0xF0000010 — originally labeled "SOS alive", later reinterpreted as "firmware validation failed (stale BIOS error)"
SOS loaded by	BIOS pre-loads SOS from SPI flash (type 1 in $PSP table)
Our loading	Mailbox CMD 0x8 also works; after BOOT_SEQ, C2PMSG_81 = 0x00000000
GPCOM ring	❌ NOT supported — no firmware version on this BIOS supports ring protocol
"Bo0m" signature	Unique custom SOS signature at 0x106A4 — not standard AMD
v3 vs v5 SOS	94% of bytes differ between BIOS v3 and v5 SOS binaries. v3 SOS may support GPCOM ring (untested)
Linux comparison	Linux psp_v11_0_8 skips all SOS loading — relies on BIOS pre-loaded SOS
SOS size	~42KB (43008 bytes), offset 0x8E0400 in BIOS
5. KIQ Status
Aspect	Detail
KIQ_BASE (0xE060)	✅ Writable
KIQ_SIZE (0xE068)	❌ Read-only = 0 — hardware thinks ring has 0 bytes; PRIMARY BLOCKER for hardware ring processing
KIQ_WPTR (0xE078)	✅ Writable (9-bit, mask 0x1FF — max 512 dwords)
KIQ_RPTR (0xE06C)	✅ Writable but does not advance when WPTR kicked
KIQ_ACTIVE (0xE080)	✅ Writable
CP_HQD registers (0xDAC0+)	❌ All NBIO-blocked — writes silently dropped
GRBM_GFX_INDEX (0x34D0)	✅ Writable (selects ME=1 correctly)
GRBM_GFX_CNTL (0x2022/0x4968)	❌ Dead — BC-250 doesn't have this register
MEC firmware	✅ Loaded via PSP mailbox (version 0x90)
MEC execution	✅ Verified — corrupting MEC ucode changes SCRATCH behavior
Software PM4 executor	✅ Working fallback — translates PM4 to direct MMIO writes
KIQ_SIZE=0 fix	Impossible — hardware-level check, not patchable in firmware
Three KIQ paths:

Path 1 (PSP KIQ via GPU proxy): ❌ KIQ_SIZE=0 block
Path 2 (GFX ring alias 0xDA60+): ❌ BASE_LO read-only=0, RPTR doesn't advance
Path 3 (Software PM4 executor): ✅ Working
6. All Known Bugs (Fixed & Unfixed)
CRITICAL FIXED BUGS:

#	Description	File:Fix
1 (Agent)	Spinlock held during ZwCreateFile in PspGpuProxyInit (DISPATCH_LEVEL violation)	PspCore.c:69-71 — Release lock before PspOpenGpuDriver(), re-acquire after
2 (Agent)	IOCTL_PSP_GPU_PM4_SUBMIT size check requires full 268-byte struct	Dynamic check via FIELD_OFFSET(..., Commands[req->CommandCount])
3 (Agent)	METHOD_BUFFERED buffer sharing — RtlZeroMemory clears req->CommandCount	Save/restore cmdCount/waitMs around zero
4 (Agent)	NBIO unlock uses GPU BAR5 instead of PSP BAR0	Always use devExt->MmioBase
5 (Agent)	Handle leak race — g_GpuDriverHandle set but proxy not available	Close handle via ZwClose on error
6 (Agent)	GRBM_STATUS reads offset 0x2004 (CC_CONFIG) instead of 0x2000	All 6 occurrences changed to 0x2000
7 (Agent)	Error string missing in KdPrint	Deferred (LOW)
RLC	RLC_CP_SCHEDULERS at 0xECA1 not 4-byte aligned	Uses 0xECA8 (empirically confirmed)
IOCTL	Name collision between GPU/PSP BAR5_READ_PROXY	PSP uses IOCTL_AMDBC250_BAR5_READ_PROXY_RAW (0x900)
Return	PspGpuProxyWriteRegister return value ignored	Checked in PspKiqInit/PspKiqSubmit
Race	g_GpuDriverHandle proxy init race	Added spinlock + g_GpuProxyInitialized guard
UNFIXED BUGS (documented but not fixed as of AGENTS.md):

#	Description	Proposed Fix
1	C2PMSG_81 save/restore in PspSendMailboxCommand — overwrites PSP response	Remove save/restore; poll C2PMSG_35 only
2	Spinlock held during polling (KeStallExecutionProcessor 500ms at DISPATCH_LEVEL)	Release spinlock before polling C2PMSG_35
3	Missing C2PMSG_37 write for high 32 bits of firmware PA (>4GB broken)	Write (ULONG)(devExt->FwPhysical.QuadPart >> 32) to C2PMSG_37
4	Shared g_CmdBuffer race between ring operations	Allocate per-call command buffer via ExAllocatePool2
5	LOAD_EMBEDDED_FW allocates SYSDRV size but copies SOS data	Use g_SosFirmwareSize for allocation
6	BOOT_SEQUENCE always returns STATUS_SUCCESS	Return real NTSTATUS from failing step
7	Test tool reads wrong struct (2 ULONGs vs 1 ULONG)	Change test tool to read single ULONG
8	REG_PROG write-only, broken for reads	Added IsRead flag — use direct PSP MMIO read when flag set
TEST TOOL BUGS (fixed):

#	Bug	Fix
1	PM4 header 0xC0370003 swapped count/opcode	0xC0043700 (correct)
2	WRITE_DATA CONTROL 0x10100000 wrong DST_SEL	0x00000102
3	RPTR comparison always succeeds (false positive)	Compare before/after difference
7. Build & Install Process
Build: build.bat produces output\PspDriver.sys, .inf, .cat, .bin files

Install via Device Manager:

PspDriver.inf has [Firmware_Files] section
Auto-copies Sysdrv.bin, Sos.bin, Smu.bin to C:\Windows\System32\drivers\bc-250\
No separate xcopy needed
GPU driver must be installed first (maps BAR5 for PSP proxy)
Secure Boot OFF + Test Signing ON required
Firmware files:

File	Source (BIOS $PSP)	Size
Sysdrv.bin	Type 8, offset 0x8FF000	262,656 bytes (256KB)
Sos.bin	Type 1, offset 0x8E0400	43,008 bytes (42KB)
Smu.bin	N/A (external)	267,970 bytes
BIOS versions: v3 and v5 have identical $PSP table structure but SOS/SYSDRV binaries are 94%/61% different

Test tools: output\toc-load-test.exe (SMU TOC load), output\psp-status-test.exe (register check), test-psp-driver.exe (comprehensive CLI test)

8. What Works vs What Doesn't
WORKING ✅

Feature	Notes
BAR5 MMIO mapping (0xFE800000, 2MB)	Direct or via GPU proxy
Mailbox CMD 0x4 (SYSDRV)	✅
Mailbox CMD 0x8 (SOS)	✅
Mailbox direct FW loading (all types: ME/PFP/CE/MEC/MEC2/RLC/SDMA)	✅ Status=0x00000000
NBIO unlock (sigs + GC/MMHUB/HDP)	✅
GC register reads/writes at corrected offsets (0x3260+)	✅ Unlocked (not NBIO-blocked)
MEC firmware loading + execution verification	✅
Software PM4 executor	✅
GPU driver proxy (BAR5 read/write fallback on Win11 26100)	✅
PSP status check via C2PMSG_81	✅
Register read/write via BAR5 proxy IOCTLs	✅
NOT WORKING ❌

Feature	Reason
GPCOM ring protocol	SOS firmware limitation — C2PMSG_64 bit 31 never sets
RBI ring protocol	SOS firmware limitation — not implemented
TMR initialization	Requires working ring protocol
GPU FW loading via ring	Requires working ring protocol
Register programming via ring (PROG_REG)	Mailbox accepts but write silently ignored
SMU firmware loading via TOC	IOCTL implemented but untested (needs reboot)
SMU wake	SMU registers all 0 — no response
SMN writes (via PCI config)	Read-only from host
KIQ hardware ring processing	KIQ_SIZE=0 read-only at hardware level
CP_HQD register writes (0xDAC0+)	NBIO-blocked
GFX engine ring processing	BASE_LO read-only, RPTR doesn't advance
Compute/Dispatch	All COMPUTE addresses corrected but engines dead
3D graphics	❌ Impossible (no compute/GFX engine processing)
Display (WDDM)	❓ Untested on Win11 26100
UNKNOWN ❓

Feature	Status
v3 BIOS SOS ring support	Untested — SOS binaries 94% different from v5
SMU init via correct SMU command interface	SMU 0x015400F0/F4/F8 interface untested via PSP BAR5
NBIO full unlock after SOS+SMU init	Linux doesn't need NBIO unlock — may auto-unlock
VBIOS loading from ACPI VFCT	Not implemented in our driver
Ring creation with proper VM inv eng assignment	Not implemented — needs working engines first
PRIMARY BLOCKERS (in order):

SMU firmware not running → GFX/CP blocks have no clock/power
KIQ_SIZE=0 read-only → hardware ring processing impossible
GPCOM ring protocol unsupported → TMR and GPU FW loading via ring blocked
No VBIOS access → missing power-up init sequences
CP_HQD registers NBIO-blocked → alternative KIQ init path blocked

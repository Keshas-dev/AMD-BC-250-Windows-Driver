Search the workspace `C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main` for any Linux amdgpu source code, analysis files, or references to:
- psp_v11_0_8.c
- cyan_skillfish*.c
- gfx_v10_0.c
- amdgpu_gfx.c (ring init code)
- Any .c or .h files from Linux kernel
- docs/LINUX-AMDGPU-ANALYSIS.md
- docs/NBIO-FIREWALL-ANALYSIS.md

Also search for files containing "cyan_skillfish" or "cyan skillfish" or "BC-250" or "13FE" in the docs/ directory.

For each file found, show its path and the key sections (especially about ring initialization, CP ring setup, MEC pipe init, GFX ring init).

Be very thorough - search across the entire workspace including any subdirectories.
The user wants me to thoroughly search the workspace for Linux amdgpu source code, analysis files, and references to specific files and patterns. Let me break this down into multiple searches:

Glob for specific files
Grep for specific patterns
Read specific files if found
Let me start with multiple parallel searches.

Now let me search for the specific patterns in the docs/ directory and across the codebase.

Now let me search for specific content patterns across the workspace:

Good, now I need to read the key documents that were found. Let me read the most important ones:

docs/LINUX-AMDGPU-ANALYSIS.md - the main analysis document
docs/NBIO-FIREWALL-ANALYSIS.md - NBIO analysis
Also check for any MEC/ring init code references
Let me also search for more specific patterns related to ring initialization.

Now let me read the key analysis documents and search for more specific patterns:

Now let me search for more specific content related to ring initialization code in the source files and any references to Linux kernel source files.

Let me search deeper for ring initialization specifics and Linux kernel source references:

Now let me read the key source files that contain the ring initialization code and golden register tables, along with some of the other important files.

Let me now read the key source files containing ring initialization and golden register code:


###############################################


Here is a comprehensive report of all findings:

Search Results: Linux amdgpu References in C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main
Legend
Direct Linux kernel file references — patterns like psp_v11_0_8.c, gfx_v10_0.c, etc.
Linux kernel source URLs — references to GitHub kernel tree
Offset headers — cyan_skillfish_ip_offset.h, mp_11_0_8_offset.h, etc.
Analysis documents — detailed technical breakdowns
Implementation files — Windows driver code that mirrors Linux logic
1. docs/LINUX-AMDGPU-ANALYSIS.md — PRIMARY ANALYSIS DOCUMENT
Path: C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\docs\LINUX-AMDGPU-ANALYSIS.md (276 lines)

Key Linux source files referenced:

Linux File	Path in analysis	Purpose
psp_v11_0_8.c	Line 5	Dedicated PSP driver for CYAN_SKILLFISH2
psp_v11_0_8.h	Line 6	PSP v11.0.8 header
psp_v11_0.c	Line 7	Common Navi10 PSP functions
mp_11_0_8_offset.h	Line 8	Register offsets for CYAN_SKILLFISH2
soc15.h	Line 9	SOC15 register access macros (RREG32_SOC15/WREG32_SOC15)
nbio_v7_4.c	Line 10	NBIO registers (PSB/PSP access control)
PSP VTable (psp_v11_0_8_funcs) — lines 63-73: Covers the full psp_funcs structure showing NULL for init_microcode, bootloader_load_sysdrv, bootloader_load_sos — meaning Linux skips all firmware loading for Cyan Skillfish2. Only ring_create, ring_stop, ring_destroy are implemented (from common Navi10).

Linux register offsets table — lines 107-117: Provides the full MP0 register map (C2PMSG_35, 36, 64, 67, 69, 70, 71, 81, 101) with DWORD/byte offsets.

PSP bootloader sequence — lines 245-262: Detailed 12-step PSP firmware loading sequence from Linux.

2. docs/NBIO-FIREWALL-ANALYSIS.md — NBIO FIREWALL ANALYSIS
Path: C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\docs\NBIO-FIREWALL-ANALYSIS.md (61 lines)

Status: RESOLVED — NBIO does NOT block GC registers at corrected BC-250 offsets.

Key findings:

Linux cyan_skillfish_ip_offset.h GC_BASE__INST0_SEG0 = 0x1260 discovery (line 13)
Linux CachyOS devmem results confirming all GC registers readable (lines 24-30)
Timeline of discovery (lines 56-61)
3. docs/Analize.md — COMPREHENSIVE INIT FLOW ANALYSIS
Path: C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\docs\Analize.md (189 lines)

Ring initialization sections:

GFX Ring Init (step 4) — lines 43-50: 2MB buffer + CP halt + base regs + CP resume, but NO CP firmware loaded
CP Init (what's missing) — lines 82-99: Lists all missing firmware (PFP, ME, CE, MEC, RLC) that Linux gfx_v10_0.c loads
SDMA Ring Init (step 5) — lines 52-55: 512KB buffer + base regs, but NO firmware
KIQ Ring — line 172: explicitly STATUS_NOT_IMPLEMENTED
Compute Rings — line 171: disabled by quirk
4. Source Code with gfx_v10_0.c References
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd\amdbc250_dream_hw_init.c (957 lines)

Line 22: - drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c — header comment
Lines 364-366: Golden register arrays from gfx_v10_0.c (currently stubbed)
Lines 730-758: DreamV3InitCommandProcessor — CP init (scratch test only, no firmware load)
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd\amdbc250_dream_kmd.c (4805 lines)

Line 23: - drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c — header comment
5. GFX Ring Initialization Code
GFX Ring Buffer Setup — amdbc250_dream_hw_init.c:396-486:

1. Allocate 2MB contiguous ring buffer + 4KB fence page
2. Halt CP (CP_ME_CNTL = ME_HALT | PFP_HALT)
3. Write ring base LO/HI (0xC800/0xC804) - 256B aligned
4. Calculate ring size (log2 of DWORDs)
5. Write ring control (0xC808) - size, block size, RPTR writeback enable
6. Zero RPTR (0xC80C) and WPTR (0xC818)
7. Call DreamV3InitCommandProcessor (scratch test)
8. Resume CP (write 0 to CP_ME_CNTL)
GFX Ring Submit — amdbc250_dream_kmd.c:1430-1449:

Writes WPTR to AMDBC250_REG_CP_GFX_RING0_WPTR (0xC818) via MMIO
Uses DreamV3SubmitGfxRing after building PM4 packets
IH Ring Setup — amdbc250_dream_hw_init.c:492-536:

256KB contiguous buffer
Base @ 0x3800, control @ 0x3808, RPTR @ 0x3810
IH_CNTL @ 0x3820 with ENABLE_INTR | RPTR_REARM
SDMA Ring Setup — amdbc250_dream_hw_init.c:542-581:

512KB buffer
Base @ 0xE000/0xE004, control @ 0xE008, pointers @ 0xE00C/0xE010
NOTE: amdbc250_hw_extra.h has alternate SDMA offsets at 0x1260+ (GC_BASE shifted)
6. MEC/Compute Ring Register Definitions
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc\amdbc250_dream_hw.h:208-232:

AMDBC250_REG_CP_MEC_CNTL        0x0000C0E0  /* MEC (compute) control */
AMDBC250_REG_CP_MEC_STATUS      0x0000C0E4  /* MEC status            */
AMDBC250_REG_CP_COMPUTE_RING0_BASE_LO  0x0000C900
AMDBC250_REG_CP_COMPUTE_RING0_CNTL     0x0000C908
AMDBC250_REG_CP_COMPUTE_RING0_RPTR     0x0000C90C
AMDBC250_REG_CP_COMPUTE_RING0_WPTR     0x0000C918
AMDBC250_REG_CP_HQD_ACTIVE             0x0000C860  /* HQD active mask   */
AMDBC250_REG_CP_HQD_VMID               0x0000C864  /* HQD VMID          */
AMDBC250_REG_CP_HQD_PERSISTENT_STATE   0x0000C868
AMDBC250_REG_CP_HQD_SEMA_CMD           0x0000C870
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc\amdbc250_hw_extra.h:106-108 (ALT offsets):

AMDBC250_REG_CP_MEC_CNTL      0x000080E0  /* MEC control           */
AMDBC250_REG_CP_HQD_ACTIVE    0x000080D8  /* HQD active            */
AMDBC250_REG_CP_HQD_VMID      0x000080DC  /* HQD VMID              */
Current status of compute rings: Disabled by AMDBC250_QUIRK_BROKEN_COMPUTE_QUEUE in DreamV3HwProgramGoldenRegs (line 374). MEC firmware (navi10_mec.bin) is NOT loaded.

7. PSP Proxy & KIQ Ring Code
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd\amdbc250_psp.c (529 lines)

Lines 36-39: g_GpcomRingVa is the PSP GPCOM ring, NOT a KIQ ring
Lines 42-91: PspProxyInit — opens \\.\AmdBcPsp, calls PSP_IOCTL_GET_GPU_INFO
Lines 149-159: Amdbc250PspKiqSubmit — BROKEN, returns STATUS_NOT_IMPLEMENTED
Lines 161-169: Amdbc250PspKiqReadReg — BROKEN, always returns 0xFFFFFFFF
Lines 151-154: Lists requirements for proper KIQ ring:
Separate KIQ ring buffer in system memory
KIQ doorbell register setup (via PSP mailbox or config)
PM4 packet submission via KIQ doorbell (not GPCOM)
Lines 383-399: Amdbc250PspRingCreate — PSP TOS ring creation via C2PMSG_64/69/70/71
Lines 200-529: Full PSP register access, MP0 discovery (0x40F4 range), SYSDRV/SOS loading
8. SMU v11.8 / Power Management
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\src\kmd\amdbc250_dream_power.c (856 lines)

Lines 98-137: DreamV3SmuSendMessage — implements SMU v11.8 protocol
Lines 154-185: DreamV3SmuWaitForResponse — polls C2PMSG_90
Lines 198-244: DreamV3SmuInitialize — tests SMU, gets version, but SMU does not respond (line 193: "SMU may not be running")
Line 195: References cyan_skillfish_ppt.c as Linux equivalent
SMU message IDs (from amdbc250_dream_power.c:53-77):

SMU_MSG_TestMessage        0x01  // Ping SMU
SMU_MSG_GetSmuVersion      0x02  // Firmware version
SMU_MSG_GetDriverIfVersion 0x03  // Driver interface version
SMU_MSG_RequestGfxclk      0x0E  // Request GFX clock
SMU_MSG_RequestActiveWgp   0x18  // Power up WGP CUs
SMU_MSG_QueryActiveWgp     0x1E  // Query active WGP count
SMU_MSG_SetCoreEnableMask  0x2C  // Set CU enable mask
SMU_MSG_InitiateGcRsmuSoftReset 0x2E  // GC soft reset
SMU_MSG_GetGfxFrequency    0x37  // Get current GFX frequency
SMU_MSG_ForceGfxVid        0x3B  // Force GFX voltage
SMU_MSG_UnforceGfxVid      0x3C  // Release forced GFX voltage
SMU_MSG_GetEnabledSmuFeatures 0x3D  // Query enabled DPM features
9. Register Offset Definitions with Linux References
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc\amdbc250_dream_kmd.h:946-972:

// Lines 946-953: Commentary on cyan_skillfish_ip_offset.h
// GC registers start at GC_BASE__INST0_SEG0 = 0x1260
#define AMDBC250_GC_BASE                        0x1260
#define AMDBC250_MP1_BASE                       0x16000
#define AMDBC250_THM_BASE                       0x08000     // Verified != Linux header
#define AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG  (0x1260 + 0x2004)  // 0x3264
#define AMDBC250_REG_SPI_PG_ENABLE_STATIC_WGP_MASK (0x1260 + 0x229C) // 0x34FC
#define AMDBC250_REG_GRBM_STATUS                (0x1260 + 0x2000)  // 0x3260
#define AMDBC250_REG_RLC_PG_ALWAYS_ON_WGP_MASK  (0x1260 + 0x2B04)  // 0x3D64
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc\amdbc250_dream_hw.h:311-340: SMU register offsets with explicit Linux reference:

// MP1_BASE__INST0_SEG0 = 0x16000 (from cyan_skillfish_ip_offset.h)
// mm register values from mp_11_0_8_offset.h
// Linux equivalent: SOC15_REG_OFFSET(MP1, 0, mmMP1_SMN_C2PMSG_n)
AMDBC250_REG_MP1_SMN_C2PMSG_33  0x16984  // (0x16000 + 0x0261*4)
AMDBC250_REG_MP1_SMN_C2PMSG_66  0x16A08  // (0x16000 + 0x0282*4) — message ID
AMDBC250_REG_MP1_SMN_C2PMSG_82  0x16A48  // (0x16000 + 0x0292*4) — argument
AMDBC250_REG_MP1_SMN_C2PMSG_83  0x16A4C  // (0x16000 + 0x0293*4)
AMDBC250_REG_MP1_SMN_C2PMSG_90  0x16A68  // (0x16000 + 0x029A*4) — response
10. PM4 Packet Format (GFX10/RDNA2)
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\inc\amdbc250_dream_hw.h:369-422: Full PM4 command definitions: Type 0 (reg write), Type 2 (NOP), Type 3 (executive), plus opcodes for COPY_DATA (0x40), EVENT_WRITE_EOP (0x47), NOP (0x10), INDIRECT_BUFFER (0x3F), DRAW_INDEX_AUTO, etc.

11. Linux Info Collection Script
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\collect-linux-info.sh (79 lines) Bash script that collects Linux amdgpu debugging info: PSP status, firmware files, ring buffers, MMIO regs, NBIO, SMN, DPM state. Checks for cyan_skillfish2_*.bin firmware files.

12. C# Test Tools for CP Ring Registers
C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\cp-ring-test.cs (78 lines) Tests CP ring registers: CP_ME_CNTL (0xC060), CP_MEC_CNTL (0xC0E0), RING0_BASE/CONTROL/POINTERS, GRBM_STATUS (0x3260), scratch. Tests write to CP_ME_CNTL.

C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\cp-ring2-test.cs — Tests CP ring regs at GC_BASE offsets only C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\cp-gcwrite-test.cs — Tests GC_BASE-shifted CP ring regs C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\test-tools\cp-offset-test.cs — Tests CP ring regs at multiple offsets (including 0xC800→0xDA60)

13. Files Containing "cyan_skillfish" (26 matches across 10 files)
File	Lines
AGENTS.md	142, 177, 226, 231, 243
README.md	110
inc/amdbc250_dream_kmd.h	952, 956, 959
src/kmd/amdbc250_dream_power.c	195
DEV-GUIDE.md	95, 123
test-tools/nbio-monitor.c	27
docs/LINUX-AMDGPU-ANALYSIS.md	86, 87, 88, 239, 254, 267, 268, 269
collect-linux-info.sh	56
STATUS.md	131, 132, 133
inc/amdbc250_hw_extra.h	16
14. Files Containing "BC-250" in docs/ (61 matches across 9 files)
File	Key sections
docs/NBIO-FIREWALL-ANALYSIS.md	Full NBIO analysis, GC_BASE discovery
docs/LINUX-AMDGPU-ANALYSIS.md	Linux PSP flow, firmware loading
docs/Analize.md	Complete init flow, missing CP/MEC/RLC firmware
docs/DEV-PLAN.md	Driver development roadmap
docs/BUILD-STATUS.md	Build status of each component
docs/UEFI-TOOLS-GUIDE.md	BIOS analysis tools
docs/BIOS-SETTINGS.md	P5.00 BIOS reference
docs/PILNAS-APRASAS.md	Lithuanian technical documentation, full architecture
docs/D3D12-UMD-RESEARCH.md	UMD research, DX12 DDI
docs/README.md	Documentation index
15. Files Containing "13FE" (9 matches in docs/)
File	Lines
docs/Analize.md	18
docs/LINUX-AMDGPU-ANALYSIS.md	15, 42
docs/PILNAS-APRASAS.md	105, 347, 1103, 1153
docs/README.md	21
docs/D3D12-UMD-RESEARCH.md	231
Summary of Linux Kernel Source File References
Linux Kernel File	Referenced In	Purpose
psp_v11_0_8.c	docs/LINUX-AMDGPU-ANALYSIS.md, AGENTS.md, README.md	PSP driver for CYAN_SKILLFISH2
psp_v11_0_8.h	docs/LINUX-AMDGPU-ANALYSIS.md	PSP header
psp_v11_0.c	docs/LINUX-AMDGPU-ANALYSIS.md	Common Navi10 PSP functions
gfx_v10_0.c	amdbc250_dream_kmd.c:23, amdbc250_dream_hw_init.c:22, docs/Analize.md, docs/PILNAS-APRASAS.md, docs/D3D12-UMD-RESEARCH.md	GFX10 command processor init
nbio_v7_4.c	docs/LINUX-AMDGPU-ANALYSIS.md	NBIO registers/PSP access
soc15.h	docs/LINUX-AMDGPU-ANALYSIS.md, AGENTS.md	SOC15 register macros
cyan_skillfish_ip_offset.h	AGENTS.md, amdbc250_dream_kmd.h, README.md	GC_BASE offset discovery
mp_11_0_8_offset.h	docs/LINUX-AMDGPU-ANALYSIS.md, amdbc250_dream_hw.h, AGENTS.md	MP/C2PMSG register offsets
smu_v11_8_ppsmc.h	amdbc250_dream_power.c, AGENTS.md, README.md	SMU message IDs
smu_cmn.c	amdbc250_dream_power.c, AGENTS.md, README.md	SMU mailbox protocol
cyan_skillfish_ppt.c	amdbc250_dream_power.c:195, AGENTS.md:243	BC-250 power management
nv.c	amdbc250_dream_hw_init.c:23, docs/D3D12-UMD-RESEARCH.md, docs/PILNAS-APRASAS.md	Navi family init
dcn20_hw_sequencer.c	amdbc250_dream_hw_init.c:24	DCN 2.1 display
amdgpu_gmc.c	docs/PILNAS-APRASAS.md:1203	Memory controller
No actual Linux kernel .c or .h source files are present in the workspace — only analysis documents, code references, and register offset information derived from them. The workspace does not contain any copied or extracted Linux kernel source files; all Linux knowledge is embedded as comments, documentation, and register definitions in the Windows driver codebase.
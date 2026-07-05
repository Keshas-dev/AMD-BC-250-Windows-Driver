Read and analyze the key PSP driver source files at C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\src\driver\:

1. PspDriver.c - the main driver (focus on IOCTL handlers, boot sequence, TOC loading)
2. PspCore.c - core functions (firmware loading, mailbox, proxy)
3. PspKiq.c - KIQ ring operations
4. PspSmu.c - SMU wake operations

For each file, identify:
- What the function does
- What protocols it uses (mailbox, ring, direct MMIO)
- Any bugs or suspicious code
- What's missing compared to a full initialization

Read the key sections (first 200 lines, then jump to major function blocks like IOCTL handlers, boot sequence, etc.). Look for:
- METHOD_BUFFERED issues
- Spinlock usage
- Resource leaks
- Error handling
- Missing initialization steps

Return a structured analysis organized by file, with line numbers for each finding.
The user wants me to read and analyze the key PSP driver source files. Let me start by reading the directory structure and then the key files.

$ Get-ChildItem "C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\src\driver\" -Name

PspCore.c
PspCore.h
PspDriver.c
PspKiq.c
PspKiq.h
PspSmu.c
PspSmu.h
Let me read the key files. I'll start with the main files.

I have the first 200 lines of each file. Now I need to continue reading more of each file to get the complete picture. Let me read the middle and end sections of the larger files, especially the IOCTL handlers in PspDriver.c, more of PspCore.c, and more of PspKiq.c.

Now I have a good understanding of the first 700 lines of PspDriver.c. Let me read the rest to find the IOCTL handlers and other important functions.

Now let me read the remaining IOCTL handlers in PspDriver.c:

Now I have all the code. Let me also read the header files and the PspIoctl.h to understand the structures and IOCTL definitions.

Now let me read the header files and the IOCTL definitions:

Let me read the PspIoctl.h for the IOCTL codes and structures.

Now I have a complete picture. Let me also check the firmware_data.h to understand the embedded firmware structure, and see if there are any MeCntl offset issues I noticed in the PspIoctl.h comment vs actual code.

Let me also check the firmware_data.h file for the embedded firmware constants.

Let me also check the firmware_data.h quickly for the embedded firmware constants and look at the MeCntl offset in PspIoctl.h which says 0x3814 but the correct offset should be different.

Now let me check the AGENTS.md note about GRBM_STATUS - it says: "GRBM_STATUS reads offset 0x2004 (CC_CONFIG) instead of 0x2000 (GRBM_STATUS)". The current code uses 0x2000 (which was the fix from the agent analysis). Let me verify: AMDBC250_GC_BASE=0x1260, so 0x1260+0x2000=0x3260. AGENTS.md says GRBM is at 0x3260. That seems correct.

But PspIoctl.h line 171 says: ULONG GrbmStatus; // GRBM_STATUS (GC_BASE + 0x2004 = 0x3264) - the comment says 0x2004 but the code actually uses 0x2000. So there's a documentation inconsistency but the code seems correct.

Let me also look at the MeCntl offset. PspIoctl.h line 180 says: ULONG MeCntl; // CP_ME_CNTL (GC_BASE + 0x3814 = 0x4A74). 0x1260+0x3814 = 0x4A74. That matches what AGENTS.md says (ME_CNTL at 0x4A74). OK, that's correct.

Now let me compile my comprehensive analysis.

Key findings organized by file:

PspDriver.c
PspDoBootSequence (lines 61-172):

L61-172: Boot sequence does SYSDRV + SOS + NBIO unlock. Not bad.
L86: For SOS, the allocation uses a hardcoded 262144 instead of fileSize. Wait, let me re-read:
L131: PspFreeFirmware(devExt); - frees previous firmware
L132: devExt->FwBuffer = MmAllocateContiguousMemory(262144, highAddr); - hardcodes 262144
L142: RtlZeroMemory(devExt->FwBuffer, 262144);
L143: devExt->FwSize = 262144;
L145-150: Copies file data or embedded data
This means SOS buffer is always 262144 bytes regardless of actual firmware size. If the SOS firmware is larger than 256KB, this would truncate. If smaller, it's wasteful. More importantly, the devExt->FwSize is set to 262144 even if the actual SOS firmware is smaller. This means PspSendMailboxCommand at line 155 uses this size... Wait, looking at PspSendMailboxCommand, it doesn't use FwSize directly for the mailbox command — it just passes the physical address. The mailbox command 0x00000008 (SOS load) uses the FW buffer that was set up, and the PSP hardware knows the size from somewhere else? Looking at the PSP hardware protocol, command 0x08 loads SOS via TMR — the firmware buffer contains the SOS image, and the PSP expects it at the given physical address. The PSP hardware likely determines the size from the firmware header or a fixed size, so having a fixed 262144 buffer may be fine.

PspSendSmcBoot (lines 174-328):

L184-187: Only supports embedded SMU firmware (g_SmuFirmwareData), no file loading. This is a limitation compared to LOAD_TOC IOCTL which tries file first.
L271-284: For sending the GFX command, if GPU BAR5 is available (either via direct mapping or proxy), it uses that. If not, falls back to PSP BAR0. But PSP BAR0 may not have the C2PMSG registers accessible (they're in the PSP's internal address space, typically at 0x1056C+ which might be within BAR0). This seems OK.
L302-308: After completion, reads SMU registers, but only if g_Bar5Mapping is available. If only devExt->GpuMmioBase is set, the SMU read is skipped but the rest proceeds.
DriverEntry (lines 330-400):

L365: MmMapIoSpace for PSP_BAR0_PHYSICAL (0xFD600000). This is outside the normal PCI BAR range. Let me check if this is safe.
L382: PspDoBootSequence runs at DriverEntry. If it fails, the error is logged but the device is still created. Actually looking at lines 382-385, if the boot sequence fails, the error is logged but the driver continues to create the symbolic link and return success. This means the device is registered even if PSP firmware loading fails. This might be intentional for debugging.
DriverUnload (lines 402-452):

L410-412: Order is correct - free TMR, KIQ, then firmware.
L438-446: Unmaps MmioBase and GpuMmioBase, with proper distinction between them. Looks correct.
PspDeviceControl - IOCTL Handlers:

IOCTL_PSP_INIT_HW (lines 489-590):

L497: PSP_INIT_HW_REQUEST* req = (PSP_INIT_HW_REQUEST*)inputBuffer; — This is METHOD_BUFFERED, so inputBuffer and outputBuffer are the same. But this handler reads req first (writing happens later at L586), so the buffer sharing bug shouldn't affect it. Let me think again... L584-586: bytesReturned = sizeof(ULONG); if (outputLength >= sizeof(ULONG)) { ((PULONG)outputBuffer)[0] = ... }. This writes to the same buffer. Since req was already read, this is fine — the write doesn't overwrite any input fields that were already read.
L502-505: Validates physical address. 0x400000 = 4MB max size, reasonable.
L508: if (physAddr.QuadPart == PSP_BAR0_PHYSICAL) — compares with PSP_BAR0_PHYSICAL (0xFD600000). What if the caller passes in a different BAR0 mapping? It would incorrectly go to the GPU path.
IOCTL_PSP_READ_REG (lines 592-657):

L605: if (offset >= 0xC000 && offset < 0xC200 && devExt->Bar0Base) — NBIO registers at 0xC100/0xC180 are accessed via PSP BAR0. The range 0xC000-0xC1FF is correct for NBIO but this means the read returns 0xFFFFFFFF for any unmapped register in this range (which might mask errors).
L617-631: If GpuMmioBase is set, reads directly. Otherwise falls through to proxy at L632.
L618: if (offset >= devExt->GpuMmioSize) — bounds check. Good.
IOCTL_PSP_WRITE_REG (lines 659-701):

Similar pattern to READ_REG.
L698: PspGpuProxyWriteRegister(offset, value); — return value is ignored.
IOCTL_PSP_LOAD_FW (lines 704-740):

L710: Checks inputLength > PSP_MAX_FW_TOTAL. This is correct.
L715: PspFreeFirmware(devExt); — frees previous buffer.
L718-722: Allocation with fallback to below 4GB.
Does NOT validate the firmware data. Should call PspValidateFirmware here.
METHOD_BUFFERED: Reads from inputBuffer (the shared buffer), writes to outputBuffer. No conflict since output is written after input is fully consumed.
IOCTL_PSP_SEND_CMD (lines 742-761):

L753: ULONG command = ((PULONG)inputBuffer)[0]; — reads command, then calls PspSendMailboxCommand.
L756-758: After success, writes command back to outputBuffer. Since METHOD_BUFFERED, outputBuffer == inputBuffer, so the command value is written over the input. But command was already saved, so this is safe.
IOCTL_PSP_NBIO_UNLOCK (lines 814-833):

L816-818: Reads before values. Uses devExt->MmioBase for register access. This was fixed to use devExt->MmioBase instead of GPU BAR5 — correct per AGENTS.md bug #4.
L820-822: Writes NBIO signatures. Uses devExt->MmioBase - correct.
L823: Reads MMHUB after. Correct.
L831: Compares before/after to determine success. Good verification.
IOCTL_PSP_NBIO_VIA_RING (lines 878-898):

This IOCTL is a stub — it just returns status codes without doing any real work. It claims to use "mailbox" but doesn't actually send any command.
If GpuMmioBase is set, returns 0x0B (GFX_CMD_ID_PROG_REG) in resp[0]. Otherwise returns 0 with last value 0xFFFFFFFF.
IOCTL_PSP_GET_STATUS (lines 900-981):

L916: RtlZeroMemory(info, sizeof(PSP_STATUS_INFO)); — METHOD_BUFFERED BUG: outputBuffer is same as inputBuffer, but for this IOCTL there's no input data to read from the inputBuffer (it's a get-status call with no meaningful input), so the zero is harmless. Actually, let me check... The IOCTL handler's input is just a code (no input buffer needed). The output buffer is PSP_STATUS_INFO. So the zeroing is safe here.
L947-948: Reads NBIO sigs from devExt->MmioBase — correct.
L952-958: For GPU registers, uses GpuMmioBase if available. The MeCntl at offset AMDBC250_GC_BASE + 0x3814 = 0x1260 + 0x3814 = 0x4A74 — correct.
IOCTL_PSP_BOOT_SEQUENCE (lines 1028-1131):

L1035-1045: Tries to map GPU BAR5 if not already mapped. This is a fallback for cases where INIT_HW wasn't called first.
L1063: Copies embedded SYSDRV firmware data. Note: Only uses embedded, not file. This is different from PspDoBootSequence which tries file first.
L1103: RtlZeroMemory(devExt->FwBuffer, g_SosFirmwareSize); — this is redundant since MmAllocateContiguousMemory zeros the buffer... actually it doesn't guarantee zeroing on all Windows versions.
L1115-1119: NBIO unlock after SOS. Uses devExt->MmioBase. But on L1123: READ_REGISTER_ULONG((PULONG)((PUCHAR)unlockBase + (AMDBC250_GC_BASE + 0x2000))) — reads GRBM status from the NBIO unlock base which is devExt->MmioBase (PSP BAR0). If PSP BAR0 doesn't map GPU registers (which it usually doesn't), this would read 0xFFFFFFFF. But MmioBase was possibly set to GpuMmioBase earlier.
IOCTL_PSP_LOAD_TOC (lines 1261-1429):

L1276-1282: Loads ASD, TA, SMU firmware from disk files in bc-250 directory.
L1280-1289: Falls back to embedded SMU firmware if Smu.bin not found.
L1360: RtlCopyMemory(toc + fwOffsets[entryCount - 1], smuData, smuSize); — This copies SMU firmware into the TOC buffer. But the SMU data might be g_SmuFirmwareData (embedded) or a file-loaded buffer. The later cleanup at L1361: if (smuData != g_SmuFirmwareData) ExFreePoolWithTag(smuData, 'fw'); — correct.
L1387-1398: Sends the command via C2PMSG mailbox. Uses g_Bar5Mapping if available, else devExt->GpuMmioBase. Does NOT use PSP proxy path (PspGpuProxyWriteRegister). This means on Windows 11 26100 where direct BAR5 mapping fails, the proxy path isn't tried for TOC loading. Wait, looking at the code structure — g_Bar5Mapping is set on INIT_HW success or GpuMmioBase mapping. g_GpuProxyAvailable is set in PspGpuProxyInit. But the TOC loading code at L1387-1398 doesn't check g_GpuProxyAvailable — it only uses direct MMIO. So if direct BAR5 failed but proxy works, TOC loading won't use the proxy!
This is a BUG: The TOC loader at L1387 doesn't use the proxy path. Compare with PspLoadIpFwViaMailbox (PspCore.c:383-392) which DOES check g_GpuProxyAvailable. The TOC loading skips the proxy path.

IOCTL_PSP_GPU_PM4_SUBMIT (lines 1524-1551):

L1531: PPSP_GPU_PM4_SUBMIT_REQUEST req = (PPSP_GPU_PM4_SUBMIT_REQUEST)inputBuffer;
L1532-1533: Saves cmdCount and waitMs BEFORE RtlZeroMemory on L1544. This is the fix from AGENTS.md bug #3.
L1544: RtlZeroMemory(resp, sizeof(*resp)); — zeros the output buffer (which is same as input buffer in METHOD_BUFFERED).
L1546-1547: Restores saved fields. Correct fix.
L1548: Calls PspGpuPm4Submit(devExt, req, resp) — but req and resp point to the same buffer! After the RtlZeroMemory + restore, req->CommandCount and req->WaitMs are valid, but if PspGpuPm4Submit reads other fields from req (like req->Commands[]), those were zeroed! Wait, req has fields CommandCount, Reserved, WaitMs, and Commands[64]. The RtlZeroMemory covers the entire PSP_GPU_PM4_SUBMIT_RESPONSE (which is a different struct but shares the same buffer). Let me check the struct sizes:
PSP_GPU_PM4_SUBMIT_REQUEST: CommandCount(4) + Reserved(4) + WaitMs(4) + Commands64 = 268 bytes
PSP_GPU_PM4_SUBMIT_RESPONSE: Status(4) + ScratchBefore(4) + ... + Pm4Dwords(4) = 11*4 = 44 bytes
So RtlZeroMemory zeros 44 bytes starting from the buffer. The first 12 bytes are CommandCount+Reserved+WaitMs which are restored. But Commands[0] to Commands[7] (bytes 12-43) are also zeroed! That's the first 8 DWORDs of the 64 PM4 commands!

WAIT — this IS a bug! Let me re-check:

The METHOD_BUFFERED buffer is shared. resp = (PPSP_GPU_PM4_SUBMIT_RESPONSE)outputBuffer which is the SAME as inputBuffer. RtlZeroMemory(resp, sizeof(*resp)) zeros 44 bytes.

sizeof(PSP_GPU_PM4_SUBMIT_RESPONSE) = Status(4) + ScratchBefore(4) + ScratchAfter(4) + WptrReadback(4) + KiqRingWptr(4) + KiqRingSize(4) + KiqRingPa(4) + HqdPqWptrBefore(4) + HqdPqWptrAfter(4) + HqdActive(4) + Pm4Dwords(4) = 44 bytes.

Overlap with PSP_GPU_PM4_SUBMIT_REQUEST:

Offset 0-3: CommandCount — restored after zero ✅
Offset 4-7: Reserved — not used ❓ but was zeroed
Offset 8-11: WaitMs — restored after zero ✅
Offset 12-43: Commands[0] through Commands[7] (first 8 DWORDs) — ZEROED and NOT RESTORED ❌
So the first 8 PM4 commands will be all zeros! Only CommandCount and WaitMs are restored. The actual PM4 command data in Commands[0..7] is lost.

This is a CRITICAL BUG. The fix should either: a) Save the entire Commands array before zero, or b) Use a separate output buffer, or c) Don't zero the first sizeof(PSP_GPU_PM4_SUBMIT_RESPONSE) bytes — zero only the output-specific fields.

Actually wait, let me look at PspGpuPm4Submit more carefully (PspKiq.c:372-458). It reads req->CommandCount and req->WaitMs (which were saved/restored) and req->Commands[i] (which were partially zeroed!). So yes, the first 8 commands are 0 when they should be the actual PM4 values.

BUT WAIT — I need to re-examine. The PSP_GPU_PM4_SUBMIT_RESPONSE is 44 bytes. It starts from outputBuffer which is the same as inputBuffer. The RtlZeroMemory zeros 44 bytes from the start. So:

Input buffer layout: CommandCount(4) + Reserved(4) + WaitMs(4) + Commands64 = 268 bytes
Output buffer layout: Status(4) + ScratchBefore(4) + ... + Pm4Dwords(4) = 44 bytes
The zero covers offset 0-43. This zeroes:

CommandCount → saved and restored
Reserved → wasn't used before, would be 0 anyway
WaitMs → saved and restored
Commands[0..7] → lost (first 8 of 64 commands)
So YES, this is a real bug — the first 8 PM4 DWORDs are zeroed and lost.

Hmm, but looking at the comment on line 1545: /* Restore fields that RtlZeroMemory cleared (METHOD_BUFFERED shares buffer) */ — the developer was aware of the issue but only restored CommandCount and WaitMs, not the Commands array.

This needs to be fixed.

IOCTL_PSP_REG_PROG (lines 1198-1223):

L1204: Reads req->RegId and req->RegValue from inputBuffer.
L1218: Writes directly via MMIO.
L1219: Writes output. For METHOD_BUFFERED, this writes over the input. But input fields were already read. Safe.
Now for the GrbmStatus offset issue that AGENTS.md mentioned. The agent found "GRBM_STATUS reads offset 0x2004 (CC_CONFIG) instead of 0x2000 (GRBM_STATUS)". Looking at all 0x2000 vs 0x2004 usage:

PspIoctl.h line 171: ULONG GrbmStatus; // GRBM_STATUS (GC_BASE + 0x2004 = 0x3264) — comment says 0x2004 PspIoctl.h line 212: ULONG GrbmStatus; // GRBM_STATUS (GC_BASE + 0x2004 = 0x3264) — comment says 0x2004

But code uses 0x2000 everywhere. Let me verify: According to AGENTS.md #6, "GRBM_STATUS reads offset 0x2004 (CC_CONFIG) instead of 0x2000 (GRBM_STATUS)" was fixed by the agent. Current code uses 0x2000. But the header comments still say 0x2004. Minor documentation bug.

Let me also check the PspIoctl.h for the MeCntl field comment at line 180: ULONG MeCntl; // CP_ME_CNTL (GC_BASE + 0x3814 = 0x4A74) — bit28=ME_HALT, bit30=PFP_HALT

0x1260 + 0x3814 = 0x4A74. And PspDriver.c line 957: info->MeCntl = READ_REGISTER_ULONG((PULONG)((PUCHAR)gpuBase + (AMDBC250_GC_BASE + 0x3814))); — same. OK correct.

PspCore.c analysis:
PspOpenGpuDriver (lines 24-55):

L36-46: Opens the GPU driver device. Properly uses ZwCreateFile with SYNCHRONOUS_IO_NONALERT. OK.
PspGpuProxyInit (lines 57-101):

L64: KeAcquireSpinLock(&g_Bar5MappingLock, &irql); — acquires spinlock.
L66-69: Fast path — already initialized. Releases lock and returns.
L71-80: If no handle, releases lock (L72), opens handle (L73), then re-acquires lock (L78). This is the fix from AGENTS.md bug #1 — don't hold spinlock during ZwCreateFile.
L82: Releases lock.
L84: testValue = PspGpuProxyReadRegister(0); — test reads from offset 0.
L87: If 0xFFFFFFFF, closes handle and returns error. But note: at line 84, EITHER we had a handle pre-existing OR we just opened one. The test read at offset 0 could succeed even if the proxy is not working with real GPU registers — offset 0 is GPU_ID which might read 0xFFFFFFFF for unrelated reasons.
L96-99: On success, set g_GpuProxyAvailable = TRUE and g_GpuProxyInitialized = TRUE.
BUT there's a subtle issue: After L93 returns STATUS_DEVICE_NOT_READY, the handle was already closed and set to NULL. If another thread calls PspGpuProxyInit concurrently, it would try to open the GPU driver again. But since there's no lock held during the ZwCreateFile call (L73), concurrent access could trigger multiple ZwCreateFile calls. The g_GpuProxyInitialized flag at L98 is not used as a guard in the fast path — the fast path uses g_GpuProxyAvailable && g_GpuDriverHandle != NULL. However, between L42 (when first thread releases lock) and L78 (re-acquires), another thread could also open the GPU driver.

Actually wait, looking at it more carefully:

Thread A: acquires lock, sees g_GpuDriverHandle == NULL, releases lock
Thread B: acquires lock, sees g_GpuDriverHandle == NULL, releases lock
Thread A: ZwCreateFile succeeds, sets g_GpuDriverHandle
Thread B: ZwCreateFile also succeeds, OVERWRITES g_GpuDriverHandle with its own handle → handle leak!
This is a race condition on the first init. The old handle from Thread A gets overwritten by Thread B's handle, and Thread A's handle is never closed.

Actually wait, I'm looking at g_GpuDriverHandle — it's in shared data section (.Shared). When Thread A overwrites it to a new value, Thread A's old handle is leaked. But since both open the same device, having two handles is wasteful but not immediately fatal for correctness (ZwClose on either handle works). However, the second handle is leaked because g_GpuDriverHandle only holds the last opened handle.

This is a minor resource leak. A proper fix would be:

Use InterlockedCompareExchange to ensure only one thread initializes
Or check g_GpuProxyInitialized before proceeding
Looking at the code, g_GpuProxyInitialized is set at L98 but never checked at L66. The check at L66 only checks g_GpuProxyAvailable && g_GpuDriverHandle != NULL. If g_GpuProxyInitialized were checked at L66, the race would be avoided on subsequent calls.

But g_GpuProxyInitialized is a static variable (not in shared section), so it's per-instance. The global g_GpuProxyAvailable IS in shared section. So there's a mismatch.

PspGpuProxyReadRegister (lines 103-141):

L110: Reads g_Bar5Mapping under lock, snapshots value.
L118: If no direct mapping, snapshots g_GpuDriverHandle under lock.
L124-138: Uses proxy if handle is available.
L130-133: ZwDeviceIoControlFile(localHandle, NULL, NULL, NULL, &ioStatus, IOCTL_AMDBC250_BAR5_READ_PROXY_RAW, ...) — this is the raw IOCTL value 0x900.
L132: &inputOffset, sizeof(inputOffset) — passes the offset as input.
L133: &outputValue, sizeof(outputValue) — receives the value.
But note: The GPU driver uses METHOD_BUFFERED, and the PSP driver passes inputOffset as a ULONG and receives outputValue as a ULONG. But the GPU driver's BAR5_READ_PROXY handler reads from Irp->AssociatedIrp.SystemBuffer — it expects offset to be there, and writes the value back to the same buffer. The PSP driver creates separate input and output buffers. Since METHOD_BUFFERED uses the same buffer for both, the PSP's separate buffer approach works because ZwDeviceIoControlFile with METHOD_BUFFERED copies the input buffer to the kernel's system buffer before dispatch, then copies the system buffer back to the output buffer after return. But with separate input and output, the input is copied to system buffer, then the output is copied FROM system buffer — which means if the outputLength > inputLength, the extra bytes are whatever was in the system buffer. Since outputLength=4 and inputLength=4, this is fine.

Wait, actually looking at ZwDeviceIoControlFile more carefully: For METHOD_BUFFERED, the I/O manager allocates a buffer of size max(InputBufferLength, OutputBufferLength) and copies InputBuffer into it. Then after the driver processes it, it copies the buffer back out as OutputBuffer (up to OutputBufferLength bytes). So with inputOffset (4 bytes) as input and outputValue (4 bytes) separated, the I/O manager creates a 4-byte buffer, copies the input offset into it, the driver reads the offset, reads the register, writes the value into the same buffer, and then the 4 bytes are copied to outputValue. This works correctly.

PspGpuProxyWriteRegister (lines 143-174):

Similar pattern.
L163: Passes params[2] = {offset, value} as 8-byte input, and NULL output.
L168: Uses IOCTL_AMDBC250_BAR5_WRITE_PROXY_RAW (0x901).
PspSendMailboxCommand (lines 220-303):

L225: PVOID mboxBase = devExt->GpuMmioBase ? devExt->GpuMmioBase : devExt->MmioBase;
L228-234: If no base and proxy handle exists, tries proxy init.
L248: mboxBase = (g_Bar5Mapping != NULL) ? g_Bar5Mapping : mboxBase; — prefers g_Bar5Mapping. This means if GPU driver already mapped BAR5 and set g_Bar5Mapping, even the PSP driver's own GpuMmioBase is overridden with the GPU driver's mapping. This could be an issue if the PSP driver's mapping was valid but g_Bar5Mapping is more up-to-date. Actually, g_Bar5Mapping is the GPU driver's mapping, shared via the shared data section. So it should be identical or more recent.
L250-257: Proxy path — uses PspGpuProxyWriteRegister. Checks return values. Good.
L260-276: Direct MMIO path. Writes to mboxBase.
L280-294: Poll loop, reads C2PMSG_35. For proxy path, uses PspGpuProxyReadRegister. For direct, uses READ_REGISTER.
L296-300: Timeout handling. Returns STATUS_TIMEOUT if stuck.
PspLoadIpFwViaMailbox (lines 305-444):

L323-326: Requires g_Bar5Mapping or devExt->GpuMmioBase — if no direct BAR5 mapping AND no proxy, fails. This is the loading path used for IOCTL_PSP_LOAD_IP_FW_DIRECT.
L383-392: Proxy path for writing the command. This is the CORRECT proxy handling — uses g_GpuProxyAvailable check.
L394-401: Direct MMIO path.
L406-418: Poll loop, similar to SendMailboxCommand.
L421-437: Reads C2PMSG_81 for status. Returns success if no timeout.
L441-442: Frees command buffer and firmware buffer. IMPORTANT: The firmware buffer is freed AFTER the PSP command completes. This is fine since the PSP hardware should have already DMA'd the firmware data by the time C2PMSG_35 clears.
PspInitTmr (lines 446-455):

Stub implementation. The KIQ ring protocol is not supported by this SOS (from the comment), so it just sets g_TmrInitialized if the KIQ ring is initialized. Not really initializing TMR.
PspKiq.c analysis:
PspKiqProgramHwRegisters (lines 42-87):

L53-62: Programs KIQ_BASE_LO and HI via proxy.
L65-70: Clears RPTR/WPTR.
L72-76: RLC_CP_SCHEDULERS SKIPPED — documented with comment.
L78-81: Unhalts CP (ME_CNTL = 0). This is done every time.
PspKiqInit (lines 89-196):

L105-111: InterlockedCompareExchange guard for single init. Good.
L114-122: If no MmioBase AND no GPU handle, tries proxy init. Wait, the check is !devExt->MmioBase — this is checking PSP BAR0 mapping, not GPU BAR5 mapping. If PSP BAR0 isn't mapped but GPU driver is available, it tries proxy. The logic should also check !devExt->GpuMmioBase && g_GpuDriverHandle == NULL.
L129-142: Allocates ring buffer with cache coherency (MmNonCached). Correct.
L151-170: Allocates WPTR polling page. Non-fatal if it fails (just a warning).
L177-183: After init complete, ensures GPU BAR5 access again by calling proxy init. But this was already checked earlier. Redundant but harmless.
L186-190: Programs HW registers. Returns success even if HW programming fails (kernels works in software-only mode).
PspKiqSubmit (lines 198-259):

L219-232: Capacity check using RPTR. This handles ring wrap correctly.
L234-241: Writes commands to ring, wraps at size boundary.
L246: KeMemoryBarrier() — correct for GPU DMA coherency.
L249-251: Updates WPTR in polling page.
L254-256: Updates WPTR in GPU KIQ register.
PspKiqLoadFirmware (lines 294-370):

L303-308: Auto-inits KIQ if not initialized.
L319-322: Frees previous DMA buffer before allocating new one. OK.
L326-341: Allocates DMA buffer, copies firmware, gets physical address.
L343-363: Builds PM4 command for LOAD_IP_FW (cmd_id=0x06), submits to KIQ ring.
L366-367: Does NOT free DMA buffer — it's kept alive for GPU DMA. This is intentional and documented.
PspGpuPm4Submit (lines 372-459):

L378-381: Auto-inits KIQ if needed.
L383-385: Validates request.
L388: Reads SCRATCH before.
L391: Reads WPTR before.
L393-421: Acquires spinlock, writes commands, updates WPTR, releases lock.
L424: Memory barrier.
L427-429: Updates polling page WPTR.
L432-434: Writes WPTR to GPU KIQ register — this is the "kick" that tells GPU to execute.
L440-443: Waits if requested.
L446-451: Reads results after wait. Note: resp->WptrReadback and resp->HqdPqWptrAfter both read the same WPTR register. Redundant but harmless.
L453-456: Debug output.
PspSmu.c analysis:
PspSmuWake (lines 16-63):

L24-27: Requires g_Bar5Mapping or g_GpuProxyAvailable. If only devExt->GpuMmioBase is set but g_Bar5Mapping is NULL and g_GpuProxyAvailable is FALSE, this returns STATUS_NOT_SUPPORTED. This could be an issue if only GpuMmioBase is available but proxy wasn't initialized.
L33-35: Reads before values.
L39-42: Writes argument and message to SMU C2PMSG registers.
L47-55: Polls for response with 3-second timeout.
L57-61: Reads after values on timeout.
The SMU register offsets look correct: MP1_BASE=0x16000, C2PMSG_66_OFFSET=0xA08, C2PMSG_82_OFFSET=0xA48, C2PMSG_90_OFFSET=0xA68.
The protocol is correct: write param to 82, write msg to 66, poll 90 for response.
Now let me compile the complete analysis.

Major bugs found:

CRITICAL: IOCTL_PSP_GPU_PM4_SUBMIT — first 8 PM4 commands zeroed (PspDriver.c:1544-1547) The RtlZeroMemory on the shared METHOD_BUFFERED buffer zeros Commands[0..7], and only CommandCount/WaitMs are restored. First 8 PM4 DWORDs are lost.
HIGH: IOCTL_PSP_LOAD_TOC doesn't use PSP proxy path (PspDriver.c:1387-1398) When direct BAR5 mapping fails but GPU proxy is available (Windows 11 26100 scenario), TOC loading uses only direct MMIO writes. Compare with PspLoadIpFwViaMailbox which correctly checks g_GpuProxyAvailable and uses proxy path.
HIGH: Handle leak race in PspGpuProxyInit (PspCore.c:64-101) Thread A opens GPU handle → Thread B also opens GPU handle → Thread B overwrites g_GpuDriverHandle → Thread A's handle leaked.
MEDIUM: IOCTL_PSP_BOOT_SEQUENCE uses embedded firmware only (PspDriver.c:1063) Unlike PspDoBootSequence which tries file first, this IOCTL handler only loads embedded firmware. Should attempt file loading for flexibility.
MEDIUM: GRBM_STATUS comment mismatch (PspIoctl.h:171, 212) Header comments say 0x2004 (CC_CONFIG) but code correctly uses 0x2000 (GRBM_STATUS).
LOW: PspDoBootSequence hardcodes SOS buffer to 262144 (PspDriver.c:132) Instead of using fileSize, allocates a fixed 256KB buffer. If SOS firmware is ever >256KB, this overflows.
LOW: IOCTL_PSP_WRITE_REG ignores PspGpuProxyWriteRegister return value (PspDriver.c:698) Unlike the READ paths which handle errors, WRITE via proxy silently succeeds even if the write failed.
LOW: PspSmuWake only checks g_Bar5Mapping and g_GpuProxyAvailable (PspSmu.c:24) If only devExt->GpuMmioBase is set (direct mapping via PSP's own INIT_HW), SMU wake returns NOT_SUPPORTED even though direct BAR5 access works.
HIGH: IOCTL_PSP_SEND_CMD uses shared buffer, but cmd value is safe (PspDriver.c:753-758) Actually this one is safe because the command value is read into a local variable before any write to outputBuffer.
Let me also note the architectural observations:

Overall the driver is well-structured with clear separation into core, KIQ, and SMU modules
The METHOD_BUFFERED bug pattern is well-understood and partially fixed
Error handling is reasonably thorough
NBIO unlock uses PSP BAR0 properly (not GPU BAR5)
The KIQ ring implementation is complete with proper wrap, WPTR polling page, and memory barriers
The main gap is SMU/VBIOS/RLC initialization which isn't implemented in the PSP driver at all
Here is the complete structured analysis of all four PSP driver source files:

PspDriver.c (1563 lines)
PspDoBootSequence (lines 61-172)
Boot flow: SYSDRV via mailbox (cmd=0x4) → SOS via mailbox (cmd=0x8) → NBIO unlock → GRBM_STATUS read
Tries file first (bc-250\Sysdrv.bin, bc-250\Sos.bin), falls back to embedded firmware
NBIO unlock (L163-164) correctly uses devExt->MmioBase (PSP BAR0), not GPU BAR5 ✅ (AGENTS.md bug #4 fixed)
BUG (LOW): SOS allocation at L132 hardcodes 262144 bytes instead of fileSize. If SOS firmware ever exceeds 256KB, this silently truncates.
PspSendSmcBoot (lines 174-328)
Protocol: GFX ring command via C2PMSG mailbox (cmd_id=0x20 = LOAD_TOC)
Builds TOC header + entry for SMU firmware (fw_type=7)
Missing: Only uses embedded g_SmuFirmwareData, no file loading fallback
Missing: SMU register readback (L302-308) only reads if g_Bar5Mapping is set, not if devExt->GpuMmioBase is set
DriverEntry (lines 330-400)
Maps PSP BAR0 at physical 0xFD600000 (L365)
Runs PspDoBootSequence inline (L382) — boot failure is logged but driver still returns STATUS_SUCCESS (L383-385). Device is always registered even if PSP firmware loading fails.
DO_BUFFERED_IO flag set at L395 ✅ (all IOCTLs use METHOD_BUFFERED)
DriverUnload (lines 402-452)
Clean order: TMR free → KIQ cleanup → firmware free → GPU proxy close → PCI cfg unmap → BAR0 unmap → GpuMmio unmap → symlink delete → device delete ✅
Correctly distinguishes GpuMmioBase != Bar0Base before unmapping (L438-446)
PspDeviceControl — IOCTL Handlers
IOCTL	Lines	Status	Issues
INIT_HW	489-590	✅	Compares physAddr against PSP_BAR0_PHYSICAL (0xFD600000) to choose BAR0 vs BAR5 path. Fallback to proxy if direct map fails.
READ_REG	592-657	✅	NBIO range (0xC000-0xC1FF) routed via PSP BAR0. Bounds check on GpuMmioSize. Falls back to proxy.
WRITE_REG	659-701	⚠️ LOW	L698: PspGpuProxyWriteRegister return value ignored — write silently "succeeds" even if proxy fails.
LOAD_FW	704-740	⚠️ MEDIUM	Stores firmware in contiguous memory, but no call to PspValidateFirmware (unlike LOAD_EMBEDDED_FW which validates at L1012).
SEND_CMD	742-761	✅	Safe: reads command into local variable before touching outputBuffer.
NBIO_UNLOCK	814-833	✅	Uses devExt->MmioBase (PSP BAR0). Compares MMHUB before/after to verify unlock.
NBIO_VIA_RING	878-898	⚠️ STUB	Returns fake status codes. Does NOT actually send any NBIO unlock via ring.
GET_STATUS	900-981	✅	Safe output-only (no meaningful input to corrupt).
BOOT_SEQUENCE	1028-1131	⚠️ MEDIUM	Only loads embedded firmware (L1063, L1104), unlike PspDoBootSequence which tries file first. Maps BAR5 directly if not already mapped.
LOAD_TOC	1261-1429	🔴 HIGH	Does NOT use PSP proxy path (L1387-1398). Only writes via direct MMIO (g_Bar5Mapping or GpuMmioBase), never via PspGpuProxyWriteRegister. Compare with PspLoadIpFwViaMailbox (PspCore.c:383-392) which correctly checks g_GpuProxyAvailable. On Windows 11 26100 where direct BAR5 fails, TOC loading will fail silently.
GPU_PM4_SUBMIT	1524-1551	🔴 CRITICAL	RtlZeroMemory(resp, sizeof(*resp)) at L1544 zeros 44 bytes of the shared METHOD_BUFFERED buffer. Only CommandCount and WaitMs are saved/restored (L1532-1533, L1546-1547). First 8 PM4 DWORDs (Commands[0..7] at offsets 12-43) are zeroed and LOST.
GrbmStatus Offset (PspIoctl.h vs code)
PspIoctl.h:171,212 comment says GC_BASE + 0x2004 = 0x3264 (which would be CC_CONFIG)
Code (L169, 953, 960, 1123, 1150) correctly uses 0x2000 = 0x3264 ... wait, 0x1260 + 0x2000 = 0x3260 and 0x1260 + 0x2004 = 0x3264. The agent analysis in AGENTS.md says GRBM_STATUS is at 0x3260. So the code uses 0x2000 which gives 0x3260 — code is correct, but the header comment at L171 says 0x2004 = 0x3264 which matches CC_CONFIG, not GRBM_STATUS. This is a documentation mismatch only.
PspCore.c (567 lines)
PspOpenGpuDriver (lines 24-55)
Opens \Device\AMDBC250DreamV43 via ZwCreateFile ✅
Uses FILE_SYNCHRONOUS_IO_NONALERT for blocking I/O
PspGpuProxyInit (lines 57-101)
L64: Acquires spinlock
L71-80: If no handle, releases spinlock before ZwCreateFile (L72-73), then re-acquires (L78) — ✅ Fix for AGENTS.md bug #1
L84: Test reads offset 0 via proxy
🔴 HIGH: Handle leak race — Between L72 (lock released) and L78 (lock re-acquired), concurrent thread can also open GPU driver. Both threads may succeed, but only the last handle is stored in g_GpuDriverHandle (L49/36 in PspOpenGpuDriver called from L73). Thread A's handle is leaked. The g_GpuProxyInitialized flag (L98) is never checked in the fast path (L66 checks only g_GpuProxyAvailable && g_GpuDriverHandle != NULL).
PspGpuProxyReadRegister (lines 103-141)
Direct MMIO via g_Bar5Mapping if available, else falls back to ZwDeviceIoControlFile with raw IOCTL 0x900 ✅
Handle snapshot under lock (L110, L118) prevents use-after-free ✅
PspGpuProxyWriteRegister (lines 143-174)
Raw IOCTL 0x901 for proxy path ✅
Returns TRUE/FALSE based on ZwDeviceIoControlFile result
PspSendMailboxCommand (lines 220-303)
Protocol: Write FW physical address to C2PMSG_36/37, write command to C2PMSG_35, poll C2PMSG_35 until 0
Checks both direct (g_Bar5Mapping/GpuMmioBase) and proxy paths ✅
Proper spinlock held around the write sequence (L246) ✅
L248: Prefers g_Bar5Mapping over local GpuMmioBase — OK since BAR5 mapping is identical
PspLoadIpFwViaMailbox (lines 305-444)
Protocol: GFX_CMD_ID_LOAD_IP_FW (0x06) via C2PMSG mailbox
Allocates 1024-byte command buffer + firmware DMA buffer ✅
Correctly uses proxy path (L383-392): Checks g_GpuProxyAvailable, uses PspGpuProxyWriteRegister. ✅
Frees both buffers after completion (L441-442) ✅
Missing: Should save/restore FW DMA buffer for the same reason PspKiqLoadFirmware does (GPU DMA may still be reading). But since the poll loop waits for C2PMSG_35 to clear, the PSP should have finished DMA by then. ✅
PspInitTmr (lines 446-455)
Stub: Does NOT actually initialize TMR, just marks it done if KIQ ring is initialized
PspAutoInitialize (lines 469-501)
Fallback path that maps BAR0 and BAR5 directly if INIT_HW wasn't called
PspLoadFirmwareFromFile (lines 506-567)
Reads firmware from \SystemRoot\System32\drivers\bc-250\*.bin ✅
Proper ZwCreateFile → ZwQueryInformationFile → ZwReadFile → ZwClose cycle ✅
Missing: Does not check file signature/checksum
PspKiq.c (459 lines)
PspKiqProgramHwRegisters (lines 42-87)
Programs KIQ_BASE_LO/HI, clears RPTR/WPTR, unhalts CP (ME_CNTL=0) ✅
RLC_CP_SCHEDULERS deliberately skipped (L72-76) — documented as not needed on BC-250
Uses proxy writes throughout ✅
PspKiqInit (lines 89-196)
Uses InterlockedCompareExchange guard (L105) for single init ✅
Allocates non-cached ring buffer (L129-142) and WPTR polling page (L151-170) ✅
L114: Checks !devExt->MmioBase (PSP BAR0) as proxy trigger — should also check !devExt->GpuMmioBase && g_GpuDriverHandle == NULL
Returns success even if HW register programming fails (L186-190) — software-only mode ✅
PspKiqSubmit (lines 198-259)
Proper ring capacity check using RPTR (L219-232) ✅
Handles ring wrap (L237-239) ✅
KeMemoryBarrier() before WPTR kick (L246) ✅
Updates WPTR in both polling page and register (L249-256) ✅
PspKiqLoadFirmware (lines 294-370)
Allocates DMA buffer, copies firmware, submits LOAD_IP_FW (cmd=0x06) via KIQ ring ✅
Does NOT free DMA buffer (L366-367) — intentionally kept alive for GPU DMA
Frees previous DMA buffer before allocating new one (L319-322) ✅
PspGpuPm4Submit (lines 372-459)
Auto-inits KIQ if needed (L378-381) ✅
Reads SCRATCH before/after (L388, L447) for verification ✅
Waits if req->WaitMs > 0 (L440-443) ✅
Note: This function reads req->CommandCount and req->WaitMs which must survive the RtlZeroMemory in the IOCTL handler (see critical bug above)
PspKiqCleanup (lines 261-292)
Halts CP engines, frees DMA buffer, WPTR poll page, ring buffer ✅
PspSmu.c (63 lines)
PspSmuWake (lines 16-63)
Protocol: Write argument to C2PMSG_82 (0x16A48), write msg to C2PMSG_66 (0x16A08), poll C2PMSG_90 (0x16A68) for response
Register offsets correct per mp_11_0_8_offset.h ✅
3-second timeout (L14, L47) ✅
L24: Checks g_Bar5Mapping || g_GpuProxyAvailable — missing devExt->GpuMmioBase check. If PSP driver mapped BAR5 directly via its own INIT_HW (without GPU driver proxy), SMU wake returns STATUS_NOT_SUPPORTED even though BAR5 is directly accessible.
Summary of Severity Rankings
Severity	Count	Key Issues
🔴 CRITICAL	1	GPU_PM4_SUBMIT: First 8 PM4 commands zeroed by RtlZeroMemory on shared METHOD_BUFFERED buffer
🔴 HIGH	2	LOAD_TOC doesn't use PSP proxy path; Handle leak race in PspGpuProxyInit
⚠️ MEDIUM	3	BOOT_SEQUENCE uses embedded FW only; LOAD_FW no validation; SMU wake missing GpuMmioBase check
⚠️ LOW	3	SOS hardcoded to 256KB; WRITE_REG ignores proxy return; Header comments (0x2004 vs 0x2000 mismatch)
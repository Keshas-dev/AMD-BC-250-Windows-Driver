# BC-250 SMU/SMN Discovery — 2026-07-05

## Summary
This session discovered the **complete SMU (System Management Unit) communication path** on the BC-250 mining ASIC, confirmed the SMU is running, and identified **GFXOFF** as the primary reason all prior compute/GFX tests failed.

---

## 1. SMN Access via NBIO BAR5+0x38/0x3C ⭐ CRITICAL DISCOVERY

### What was found
- `mmPCIE_INDEX2` at BAR5+`0x38`, `mmPCIE_DATA2` at BAR5+`0x3C`
- Writing an SMN address to `0x38` + data to `0x3C` generates SMN bus cycles through the NBIO
- This is exactly what Linux `WREG32_PCIE`/`RREG32_PCIE` does internally
- **Confirmed working for many SMN ranges**: FUSE (0x17400), MP1 (0x03Bxxxx), GC SMN

### What doesn't work
- Direct MMIO SMN ports at physical `0x3B10528`/`0x3B10564` — `MmMapIoSpace` returns NULL
- SMN via PCI config space (B0D0F0 + 0xB8/0xBC) — read-only, writes silently dropped
- **Must always use NBIO BAR5+0x38/0x3C path for SMN access**

---

## 2. MP1 (SMU) SMN Address Map

From Linux `smu_v11_0.h` (verified working):

| Symbol | SMN Address | Purpose |
|--------|-------------|---------|
| MP1_Public | `0x03B00000` | SMU register base |
| MP1_SRAM | `0x03C00004` | Firmware load target (NOT `0x01540000`!) |
| smnMP1_FIRMWARE_FLAGS | `0x03B10024` | Bits: INTERRUPTS_ENABLED=1 |
| smnMP1_PUB_CTRL | `0x03B10B14` | Bit 1: SMU reset control |
| C2PMSG_66 | `0x03B10A08` | Mailbox message register |
| C2PMSG_82 | `0x03B10A48` | Mailbox argument/response |
| C2PMSG_90 | `0x03B10A68` | Mailbox control (1=ready) |

### Important facts
- MP1_Rublic (`0x03B00000`) and MP1_SRAM (`0x03C00004`) read `0xFFFFFFFF` — firmware NOT loaded via SMN yet
- MP1 registers at BAR5+`0x16000` read `0` — MP1 NOT mapped into direct BAR5, only accessible via SMN
- SMU firmware was loaded by PSP bootrom (firmware file: `Smu.bin`, 267,970 bytes at `C:\Windows\System32\drivers\bc-250\`)

---

## 3. SMU Firmware Status — CONFIRMED RUNNING

- **SMU firmware version:** 88.6.0 (`0x00580600`)
- **Driver interface version:** 8
- **FW_FLAGS (`0x03B10024`):** `0x00000001` — INTERRUPTS_ENABLED = YES ✅
- **PUB_CTRL (`0x03B10B14`):** `0x00000000` — SMU reset NOT asserted ✅
- **C2PMSG_90:** `0x00000001` — SMU ready for commands ✅

The SMU firmware is an MCU that runs independently from the main GPU driver. It responds to mailbox commands via C2PMSG registers.

---

## 4. SMU v11.8 PPSMC Message IDs (Cyan Skillfish)

The `cyan_skillfish_ppt.c` in Linux uses `smu_v11_8_ppsmc.h`. Full header: https://git.zx2c4.com/wireguard-linux/plain/drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v11_8_ppsmc.h

```c
#define PPSMC_MSG_TestMessage                           0x1
#define PPSMC_MSG_GetSmuVersion                         0x2
#define PPSMC_MSG_GetDriverIfVersion                    0x3
#define PPSMC_MSG_SetDriverTableDramAddrHigh            0x4
#define PPSMC_MSG_SetDriverTableDramAddrLow             0x5
#define PPSMC_MSG_TransferTableSmu2Dram                 0x6
#define PPSMC_MSG_TransferTableDram2Smu                 0x7
#define PPSMC_MSG_RequestCorePstate                     0xB
#define PPSMC_MSG_QueryCorePstate                       0xC
#define PPSMC_MSG_RequestGfxclk                         0xE
#define PPSMC_MSG_QueryGfxclk                           0xF
#define PPSMC_MSG_QueryVddcrSocClock                    0x11
#define PPSMC_MSG_QueryDfPstate                         0x13
#define PPSMC_MSG_RequestActiveWgp                      0x18
#define PPSMC_MSG_SetMinDeepSleepGfxclkFreq             0x19
#define PPSMC_MSG_SetMaxDeepSleepDfllGfxDiv             0x1A
#define PPSMC_MSG_StartTelemetryReporting               0x1B
#define PPSMC_MSG_StopTelemetryReporting                0x1C
#define PPSMC_MSG_ClearTelemetryMax                     0x1D
#define PPSMC_MSG_QueryActiveWgp                        0x1E
#define PPSMC_MSG_SetCoreEnableMask                     0x2C
#define PPSMC_MSG_InitiateGcRsmuSoftReset               0x2E
#define PPSMC_MSG_GfxCacWeightOperation                 0x2F
#define PPSMC_MSG_L3CacWeightOperation                  0x30
#define PPSMC_MSG_PackCoreCacWeight                     0x31
#define PPSMC_MSG_SetDriverTableVMID                    0x34
#define PPSMC_MSG_SetSoftMinCclk                        0x35
#define PPSMC_MSG_SetSoftMaxCclk                        0x36
#define PPSMC_MSG_GetGfxFrequency                       0x37
#define PPSMC_MSG_GetGfxVid                             0x38
#define PPSMC_MSG_ForceGfxFreq                          0x39  ⚠️ DANGEROUS
#define PPSMC_MSG_UnForceGfxFreq                        0x3A
#define PPSMC_MSG_ForceGfxVid                           0x3B
#define PPSMC_MSG_UnforceGfxVid                         0x3C
#define PPSMC_MSG_GetEnabledSmuFeatures                 0x3D

#define PPSMC_Result_OK                    0x1
#define PPSMC_Result_Failed                0xFF
#define PPSMC_Result_UnknownCmd            0xFE
#define PPSMC_Result_CmdRejectedPrereq     0xFD
#define PPSMC_Result_CmdRejectedBusy       0xFC
```

---

## 5. SMU Mailbox Protocol

### Tested and confirmed working protocol (matches Linux smu_v11_0.c):
```
1. Poll C2PMSG_90 until it equals 1 (SMU ready, timeout 100-500ms)
2. Write 0 to C2PMSG_90 (acknowledge SMU ready)
3. Write parameter to C2PMSG_82 (if message requires one)
4. Write message ID to C2PMSG_66 (triggers SMU processing)
5. Poll C2PMSG_90 until it equals 1 (SMU done, timeout 500-1000ms)
6. Read response from C2PMSG_82
```

### Alternative: direct write without pre-check
If C2PMSG_90 == 0 (SMU idle), you can skip step 2 and just write to C2PMSG_66 directly. The SMU will process it and set C2PMSG_90 = 1.

---

## 6. SMU Enabled Features (0xDD602C7D)

From `smu11_driver_if_cyan_skillfish.h` feature bit definitions:
- Bit 0: **GFXCLK DPM** = ON ✅
- Bit 1: Power gating = OFF ❌
- Bit 2: **GFXOFF** = ON ✅ — GFX block in deep sleep!
- Bit 5: Various SOC features
- Bit 6: DF DPM
- Bit 10: CC DPM
- Bit 11: UVD
- Bit 15: VCN
- Bits 17-29: Various clock/power management

### Key interpretation
Most features are enabled but the GFX block is forced into GFXOFF deep sleep. Even though GFXCLK DPM is ON, the clock is at idle (15 MHz) because GFXOFF keeps the block unpowered.

---

## 7. Current GPU Status

| Register | Address | Value | Status |
|----------|---------|-------|--------|
| GRBM_STATUS | 0x2000 | 0x00000000 | All engines IDLE |
| GRBM_STATUS2 | 0x2004 | 0x00000000 | CC_CONFIG = 0 |
| SCRATCH | 0x32D4 | 0x4D585042 | "PREV"/"NEXT" ASCII |
| CC_GC_SHADER_ARRAY_CONFIG | 0x9C1C | readable |  |
| GFX frequency | QueryGfxclk (0xF) | 15 MHz | Idle with GFXOFF |
| Active WGPs | QueryActiveWgp (0x1E) | 0 | All disabled |
| GFX VID | GetGfxVid (0x38) | 0x63 | Current voltage |

### Why all compute tests failed
- GFXOFF gates the GFX block clock and power — registers appear readable/writable (shadow registers) but the actual compute/CP engines have no clock
- CP/MEC engine behind the ring buffer never processes because it has no power/clock
- KIQ_BASE/KIQ_SIZE hardwired to 0 (actually these might be writable if GFXOFF is disabled)
- RPTR staying at 0x01200000 is not a stuck bit — the ring engine is physically off

---

## 8. What Crashed the System

### Command: `ForceGfxFreq (0x39)` with param=80000 (800 MHz)
- SMU accepted the command (polled C2PMSG_90, got 1 = done)
- Tried to jump from 15 MHz idle to 800 MHz without proper voltage/DPM table setup
- GPU hang → display timeout → system became unresponsive
- **No blue screen** — likely a TDR (Timeout Detection and Recovery) that couldn't complete recovery
- After reboot: `amdbc250kmd` service stopped (manual start, Start=3)

### What NOT to do
- Never use ForceGfxFreq (0x39), ForceGfxVid (0x3B), SetCoreEnableMask (0x2C), RequestActiveWgp (0x18) without proper DPM tables
- These commands change power/clock state and will hang the GPU if SMU is missing clock/voltage parameters

### Safe commands (query-only)
- 0x1 TestMessage
- 0x2 GetSmuVersion
- 0x3 GetDriverIfVersion
- 0x3D GetEnabledSmuFeatures
- 0x37 GetGfxFrequency
- 0xF QueryGfxclk
- 0x38 GetGfxVid
- 0x1E QueryActiveWgp
- 0xC QueryCorePstate
- 0x13 QueryDfPstate
- 0x3A UnForceGfxFreq (safe cleanup)
- 0x3C UnforceGfxVid (safe cleanup)

### Medium risk (may work but need careful testing)
- 0x35 SetSoftMinCclk (needs valid frequency param)
- 0x36 SetSoftMaxCclk (needs valid frequency param)
- 0x19 SetMinDeepSleepGfxclkFreq
- 0x1A SetMaxDeepSleepDfllGfxDiv

### High risk (will hang without DPM tables)
- 0xE RequestGfxclk (timed out — likely needs driver tables)
- 0x39 ForceGfxFreq (crashed at 800 MHz)
- 0x2C SetCoreEnableMask (crashed)
- 0x18 RequestActiveWgp (crashed)
- 0x2E InitiateGcRsmuSoftReset
- 0x2F-0x31 CAC weight operations
- 0x34 SetDriverTableVMID
- 0x4/0x5 SetDriverTableDramAddrHigh/Low (requires valid system memory address)
- 0x7 TransferTableDram2Smu (requires driver table set up first)

---

## 9. Linux Comparison

Linux amdgpu on Cyan Skillfish2 successfully:
- Creates 8 compute rings (comp_1.0.0 through comp_1.3.1)
- Creates KIQ ring (kiq_0.2.1.0) and GFX ring (gfx_0.0.0)
- 24 CUs detected (SE 2 × SH 2 × CU 10, 24 active)
- Full SMU DPM initialization with clock/voltage profiles
- All firmware loaded: ME v0x63, PFP v0x94, CE v0x25, MEC v0x90, RLC v0x0d, SDMA v0x34, SMC v88.7.1
- SMU initialized: `SMU is initialized successfully!` in dmesg

### Why Windows doesn't work (yet)
Linux achieves compute by:
1. `amdgpu_discovery_set_smu_ip_blocks` adds SMU v11_0 IP block (for Cyan_Skillfish2)
2. `smu_v11_0_init_power` initializes power management
3. `cyan_skillfish_init_smc_tables` loads DPM tables
4. SMU takes over clock management — GFX clocks rise to operational frequencies
5. `amdgpu_gfx_off_ctrl(adev, false)` disables GFXOFF when submitting work
6. Rings get created with BASE_LO = real address (KIQ_SIZE != 0)

Our Windows driver is missing steps 1-5 — SMU is alive but never told how to manage clocks, so GFX stays in deep sleep.

---

## 10. Cyan Skillfish vs Cyan Skillfish2

From Linux kernel commit `94bd7bf`:
```c
case IP_VERSION(11, 0, 8):
    if (adev->apu_flags & AMD_APU_IS_CYAN_SKILLFISH2)
        amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
    break;
```

- **Cyan Skillfish** (original BC-250): NO SMU IP block added (SMU not used)
- **Cyan Skillfish2** (BC-250B): SMU v11_0 IP block added, SMU fully managed

Our device has:
- IP_VERSION(0x02009) = 10.1.3 (GC), IP_VERSION(0x0202C) = 11.0.8 (MP0/PSP)
- SMU version 88.6.0 running
- PSP firmware loaded SOS and SMU firmware

This means our card is almost certainly **BC-250B / Cyan Skillfish2** and SMU MUST be initialized for any compute to work.

---

## 11. Next Steps

When `amdbc250kmd` is restarted with admin:
1. Test `SetSoftMinCclk (0x35)` with param = 20000 (200 MHz) — low risk
2. Test `SetSoftMaxCclk (0x36)` with param = 40000 (400 MHz) — low risk
3. Query `GetGfxFrequency (0x37)` — see if clock increased
4. If clock increased: test `QueryActiveWgp (0x1E)` — see if WGPs activated
5. If clocks stable: implement full DPM initialization in `amdbc250_dream_power.c`
6. After DPM running: retry RLC firmware load via PSP + PM4 ring submission

### Long-term path
- Allocate system memory for cyan_skillfish DriverPPTable structure
- Send `SetDriverTableDramAddrHigh (0x4)` + `SetDriverTableDramAddrLow (0x5)`
- Send `TransferTableDram2Smu (0x7)` to load DPM parameters
- SMU takes over and automatically manages clocks/when GFXOFF is disabled
- Compute engines become active when GPU power management is stable

---

## 12. Files

### Modified
- `AGENTS.md` — agent analysis history
- `inf/amdbc250_dream.inf` — firmware installation paths

### New test tools
- `test-tools/bar5-smn-test.c` — **PRIMARY**: SMU mailbox via SMN (NBIO BAR5+0x38/0x3C)
- `test-tools/smu-direct-test.c` — SMU direct BAR5 (deprecated, reads 0)
- `test-tools/smu-mailbox-test.c` — PSP mailbox firmware load test
- `test-tools/smu-scan-test.c` — GPU register scan for SMU/THM/SMUIO/CLK

### Build scripts
- `compile-bar5-smn.bat` — compiles bar5-smn-test.c
- `compile-smu-direct.bat` — compiles smu-direct-test.c
- `compile-smu-mailbox.bat` — compiles smu-mailbox-test.c
- `compile-smu-scan.bat` — compiles smu-scan-test.c

### Driver files
- `src/kmd/amdbc250_dream_power.c` — `DreamV3SmuSendMessage` uses BAR5 C2PMSG (reads 0, needs SMN rewrite)
- `src/driver/PspCore.c` — PSP proxy register read/write via GPU driver
- `src/kmd/amdbc250_dream_kmd.c` — kmd main entry
- `src/kmd/amdbc250_dream_hw_init.c` — hardware init

### Documentation
- `docs/BC250-LINUX-IP-MAP.md` — verified Linux IP offset map
- `docs/REGISTER-MAP-BC250.md` — register definitions
- `docs/RING-INIT-STATUS.md` — ring buffer status
- `docs/PSP-PROXY-BYPASS.md` — PSP proxy info

---

## 13. Key Takeaways

1. **SMU is alive, running, and responsive** — mailbox communication confirmed
2. **SMN access via NBIO BAR5+0x38/0x3C works** — this is the new standard access path
3. **GFXOFF is the primary blocker** — GFX block is in deep sleep (15 MHz, 0 WGPs)
4. **ForceGfxFreq without DPM tables = system crash** — DPM must be set up first
5. **BC-250B uses SMU v11.8** — proper driver table initialization is the path forward
6. **Linux proves it works** — full initialization sequence exists in `cyan_skillfish_ppt.c`

The hardware is capable. The SMU is responsive. We just need to properly initialize DPM to wake up the GFX engines.

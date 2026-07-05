#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;
static BOOL WriteReg(uint32_t offset, uint32_t value) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static uint32_t SmnRead(uint32_t smnAddr) {
    WriteReg(0x38, smnAddr); ReadReg(0x38); return ReadReg(0x3C);
}
static BOOL SmnWrite(uint32_t smnAddr, uint32_t value) {
    if (!WriteReg(0x38, smnAddr)) return FALSE; return WriteReg(0x3C, value);
}

#define SMN_C2PMSG_66 0x03B10A08
#define SMN_C2PMSG_82 0x03B10A48
#define SMN_C2PMSG_90 0x03B10A68

static void WaitMs(uint32_t ms) { Sleep(ms); }

// Wait for SMU ready (C2PMSG_90 != 0)
static BOOL SmuWaitForReady(uint32_t timeoutMs) {
    for (uint32_t i = 0; i < timeoutMs; i++) {
        uint32_t ctrl = SmnRead(SMN_C2PMSG_90);
        if ((ctrl & 1) != 0) return TRUE;
        WaitMs(1);
    }
    return FALSE;
}

// Wait for SMU response (C2PMSG_90 != 0 after command)
static BOOL SmuWaitForResponse(uint32_t timeoutMs) {
    return SmuWaitForReady(timeoutMs);
}

// Send message without parameter
static BOOL SmuSendMsg(uint32_t msgId) {
    if (!SmuWaitForReady(1000)) return FALSE;
    SmnWrite(SMN_C2PMSG_90, 0);  // Acknowledge
    SmnWrite(SMN_C2PMSG_66, msgId);
    if (!SmuWaitForResponse(1000)) return FALSE;
    return TRUE;
}

// Send message with parameter, get response
static uint32_t SmuSendMsgWithParam(uint32_t msgId, uint32_t param) {
    if (!SmuWaitForReady(1000)) return 0xFFFFFFFF;
    SmnWrite(SMN_C2PMSG_90, 0);  // Acknowledge
    SmnWrite(SMN_C2PMSG_82, param);
    SmnWrite(SMN_C2PMSG_66, msgId);
    if (!SmuWaitForResponse(1000)) return 0xFFFFFFFE;
    uint32_t resp = SmnRead(SMN_C2PMSG_82);
    return resp;
}

int main() {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL\n"); return 1; }
    printf("GPU driver OK\n\n");

    // Check initial SMU state
    printf("=== Initial SMU State ===\n");
    printf("FW_FLAGS: 0x%08X\n", SmnRead(0x03B10024));
    printf("C2PMSG_90: 0x%08X\n", SmnRead(SMN_C2PMSG_90));
    printf("C2PMSG_82: 0x%08X\n", SmnRead(SMN_C2PMSG_82));

    // Test SMU mailbox protocol
    printf("\n=== SMU Mailbox Communication ===\n");
    
    // Message 1: TestMessage (PPSMC_MSG_TestMessage = 1)
    printf("Sending TestMessage (1)...\n");
    BOOL ok = SmuSendMsg(1);
    printf("  Result: %s\n", ok ? "OK" : "TIMEOUT/FAIL");
    printf("  C2PMSG_90: 0x%08X\n", SmnRead(SMN_C2PMSG_90));
    printf("  C2PMSG_82: 0x%08X\n", SmnRead(SMN_C2PMSG_82));

    // Message: GetSmuVersion
    // From cyan_skillfish_message_map: SMU_MSG_GetSmuVersion
    // The actual index depends on SMU_MSG_MAX_COUNT order
    // Let me check the cyan_skillfish message map
    // TestMessage=0, GetSmuVersion=1 (index in cyan_skillfish_message_map)
    
    // From smu_cmn.h: smu_cmn_send_smc_msg_with_param maps index to PPSMC msg
    // The PPSMC message for GetSmuVersion is PPSMC_MSG_GetSmuVersion
    // Let me try various message IDs
    
    // Try sending various messages
    uint32_t testMsgs[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const char* testNames[] = {"TestMessage", "GetSmuVersion", "GetDriverIfVersion", 
        "SetDriverDramAddrHigh", "SetDriverDramAddrLow", "TransferTableSmu2Dram",
        "TransferTableDram2Smu", "GetEnabledSmuFeatures", "RequestGfxclk", "ForceGfxVid"};
    
    for (int i = 0; i < 10; i++) {
        uint32_t resp = SmuSendMsgWithParam(testMsgs[i], 0);
        printf("MSG %2d (%-25s): C2PMSG_82=0x%08X C2PMSG_90=0x%08X\n",
            testMsgs[i], testNames[i], resp, SmnRead(SMN_C2PMSG_90));
    }
    
    // Check graphics activity
    printf("\n=== GPU Status after SMU Comm ===\n");
    printf("GRBM_STATUS (0x3260): 0x%08X\n", ReadReg(0x3260));
    printf("SCRATCH     (0x32D4): 0x%08X\n", ReadReg(0x32D4));
    printf("GPU_ID      (0x0000): 0x%08X\n", ReadReg(0x0000));
    
    CloseHandle(g_hDev);
    printf("\n=== DONE ===\n");
    return 0;
}

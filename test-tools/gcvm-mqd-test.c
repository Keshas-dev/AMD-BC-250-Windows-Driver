#include <windows.h>
#include <stdio.h>
#include <memory.h>

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
static HANDLE gH;
static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B88, &in, 8, &out, 8, &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B8C, &in, 8, &out, 8, &br, NULL);
}
static void WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096 + 12];
    ((PULONG)buf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)buf)[1] = (ULONG)(pa >> 32);
    ((PULONG)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C10, buf, 12 + size, NULL, 0, &br, NULL);
}
static ULONG ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inbuf[24], outbuf[4096];
    ((PULONG)inbuf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)inbuf)[1] = (ULONG)(pa >> 32);
    ((PULONG)inbuf)[2] = size;
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C14, inbuf, 12, outbuf, sizeof(outbuf), &br, NULL);
    if (br > 0) memcpy(data, outbuf, min(br, size));
    return br;
}

#define SHADER_ADDR  0xC0100000ULL
#define FENCE_ADDR   0xC0600000ULL

static UINT64 ringPa = 0;

static void SetupGCVM(void) {
    UCHAR ptBuf[256] = {0};
    DWORD br = 0;
    BOOL ptOk = DeviceIoControl(gH, 0x8000098C, NULL, 0, ptBuf, sizeof(ptBuf), &br, NULL);
    if (!ptOk) { printf("GCVM_PT_SETUP failed: %d\n", GetLastError()); return; }
    ULONG* pt = (ULONG*)ptBuf;
    UINT32 result = pt[10];
    ringPa = ((ULONG64)pt[2] << 32) | pt[1];
    printf("GCVM: result=0x%08X ringPA=0x%016llX\n", result, ringPa);
    if (result == 0xCAFEBABE) {
        UINT32 cntl = R(0xB460);
        if (!(cntl & 1)) { W(0xB460, cntl | 1); W(0x6C10, 1); W(0x6C0C, 1); Sleep(10); printf("GCVM enabled\n"); }
        else printf("GCVM already enabled\n");
    }
}

static void LoadMQD(void) {
    UINT32 mqd[512] = {0};
    mqd[0] = 0;
    mqd[1] = (UINT32)(SHADER_ADDR >> 8);  /* PGM_LO */
    mqd[2] = 0;                             /* PGM_HI */
    mqd[3] = 0x00000000;                    /* RSRC1 */
    mqd[4] = 0x00000000;                    /* RSRC2 */
    WritePhys(ringPa, mqd, 2048);
    W(0x9104, (UINT32)(ringPa & 0xFFFFFFFF));
    W(0x910C, 1); Sleep(50);
    printf("MQD loaded: PGM_LO=0x%08X ACTIVE=%d\n", R(0x8110), R(0x910C));
}

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERR\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    /* Shader: just s_endpgm */
    UINT32 shader[4] = {0, 0, 0, 0};
    shader[0] = 0xBF9F0000;  /* s_endpgm */
    WritePhys(SHADER_ADDR, shader, 16);

    /* Write a shader that actually WRITES a value to FENCE_ADDR */
    UINT32 writeShader[16];
    /* From RDNA ISA:
     * s_mov_b32 s0, 0x12345678    ; move test value to SGPR0
     * s_mov_b64 s[2:3], FENCE_ADDR ; move addr to SGPR[2:3]
     * s_store_dword s0, s[2:3], 0 ; store s0 to memory[s[2:3]+0]
     * s_endpgm
     * 
     * s_mov_b32 encoding: BE80_0000_imm32 (s_mov_b32 s0, imm32: BEA0_0000 + (sdst << 16))
     * Actually: BE80 0000 <imm32> for s_mov_b32 s0 (sdst=0, op=0xBEA0_0000? Let me check)
     * Encoding: 0xBEFC_00XX for VOP? No, s_mov_b32 is SOP1.
     * SOP1: [31:23]=BE80_XX01... hmm
     * s_mov_b32 sdst, simm32: 0xBE800001 | (sdst << 0)
     * Let me use the encoding from RDNA3 ISA doc (same base for RDNA1):
     * SOP1 instruction: ENCODING = (op << 23) | (sdst << 0) | (ssrc0 << 8) | (1<<30)
     * s_mov_b32: op[6:0] = 0x09 (from SOP1 table)
     * Wait, different encoding schemes...
     * 
     * Simpler: just write a literal at FENCE_ADDR via s_endpgm variant.
     * s_endpgm encoding: 0xBF9F0000 reliably terminates.
     * 
     * For actual memory write shader: let's try a flat_store approach.
     */
    
    /* Let me use a known-good write via VGPR:
     * v_mov_b32 v0, 0x12345678   ; VOP1: 0x7E000200 | (vdst << 0) | (src0_sel << 9)
     * v_mov_b32 v1, lo32(FENCE)  ; store addr low
     * v_mov_b32 v2, hi32(FENCE)  ; store addr hi = 0
     * flat_store_dword v[1:2], v0
     * s_endpgm
     * 
     * For VOP1: encoding = (0x7E << 25) | (op << 17) | (vsrc1 << 24)?? Actually:
     * VOP1: BIT31-25=0x7E | BIT24 |= vsrc1_sel | BIT17-9 = VDST | BIT8-0 = OP
     * v_mov_b32: OP = 0x02 (V_MOV_B32), VDST=v0, SRC0=literal constant
     * Encoding: 0x7E000200 (op=2, vdst=0, vsrc=literal)  <-- wait, this has wrong field assignment.
     *
     * Actually v_mov_b32 v0, imm32 = 0x7E000200 (VDST=0=v0 + OP=2) + imm32 as next DWORD
     * v_mov_b32 v1, imm32 = 0x7E000202 (VDST=1=v1) + imm32
     * v_mov_b32 v2, imm32 = 0x7E000204 (VDST=2=v2) + imm32
     * flat_store_dword v[1:2], v0 = ??
     * V_MOV_B32 v0, imm32: I think it is: 0xB08003F8 where... no.
     * Let me use 0xB0800000 + (vdst << 0) for v_mov_b32 with literal:
     * Actually for VOP1 with literal: word0 = B0800000 | (vdst)
     *   then word1 = literal constant
     * v_mov_b32 v0: word0=0xB0800000, word1=0x12345678
     * v_mov_b32 v1: word0=0xB0800001, word1=0xC0600000 (FENCE low)
     * v_mov_b32 v2: word0=0xB0800002, word1=0x00000000 (FENCE hi)
     * flat_store_dword v[1:2], v0: DS/DL... this is complex.
     *  
     * Actually for GFX10, v_mov_b32 v0, imm32 is encoded as:
     * ENCODING: D000_0000_7E00_02FF (64-bit? No, 32-bit with literal)
     * 0x7E000200: OP=2 (V_MOV_B32), VDST=0, VSRC1=0x3FF (VCCZ?)
     * With literal: 0xD5000000 | (VDST<<0) | (VSRC1<<9) | (OP<<17)
     * Hmm, getting complicated.
     *
     * For safety: just use s_endpgm and check for ANY compute activity.
     * If PGM_LO loads and GRBM shows non-zero after dispatch, we win.
     */
    WritePhys(SHADER_ADDR, shader, 16);
    
    UINT64 zero = 0;
    WritePhys(FENCE_ADDR, &zero, 8);

    /* Setup */
    SetupGCVM();
    LoadMQD();

    W(0x34FC, 0xFFFFFFFF);  /* SPI max */
    W(0x9C1C, R(0x9C1C) | 0x1F000000);

    printf("\n=== DISPATCH WITH DIM_X=1 ===\n");
    
    /* Method A: Write DIM_X then dispatch with GRBM select ME=1 */
    printf("--- Method A: DIM_X via MMIO + GRBM select ---\n");
    W(0x80E4, 1);  /* DIM_X = 1 */
    printf("DIM_X = 0x%08X\n", R(0x80E4));
    printf("DIM_Y = 0x%08X\n", R(0x80E8));
    printf("DIM_Z = 0x%08X\n", R(0x80EC));
    
    /* Try without GRBM select first */
    W(0x80E0, 0x0003);
    Sleep(100);
    printf("Direct: 0x80E0=0x%08X GRBM=0x%08X\n", R(0x80E0), R(0x3260));

    /* With GRBM select ME=1 */
    W(0x34D0, 0x00010000);  /* GRBM select ME=1 PIPE=0 QUEUE=0 */
    W(0x80E0, 0x0003);
    W(0x34D0, 0xE0000000);  /* broadcast */
    Sleep(100);
    printf("ME=1: 0x80E0=0x%08X GRBM=0x%08X\n", R(0x80E0), R(0x3260));

    /* Method B: Write DIM and DISPATCH while under GRBM select */
    printf("\n--- Method B: Full sequence under GRBM select ---\n");
    W(0x34D0, 0x00010000);
    W(0x80E4, 1);   /* DIM_X */
    /* Skip DIM_Y/Z - dead */
    W(0x80F0, 0);   /* START_X */
    W(0x80F4, 0);   /* START_Y */
    W(0x80F8, 0);   /* START_Z */
    W(0x80FC, 1);   /* NUM_THREAD_X */
    W(0x80E0, 0x00000003);  /* DISPATCH */
    W(0x34D0, 0xE0000000);
    Sleep(200);
    printf("Full: GRBM=0x%08X GRBM2=0x%08X\n", R(0x3260), R(0x3268));

    /* Method C: Try with RLC scheduler setting for compute queue */
    printf("\n--- Method C: RLC scheduler + dispatch ---\n");
    /* Enable scheduler for ME1/P0/Q1 (compute queue, not KIQ) */
    W(0xECA8, 0xA1);  /* ENABLE + ME1 + PIPE0 + QUEUE1 */
    W(0x34D0, 0x00010000);
    W(0x80E4, 1);
    W(0x80E0, 0x00000003);
    W(0x34D0, 0xE0000000);
    Sleep(200);
    printf("RLC=0xA1: GRBM=0x%08X\n", R(0x3260));

    /* Method D: Try with RSRC1 having proper values in MQD */
    printf("\n--- Method D: MQD with RSRC1 values ---\n");
    UINT32 mqd[512] = {0};
    mqd[0] = 0;
    mqd[1] = (UINT32)(SHADER_ADDR >> 8);
    mqd[2] = 0;
    /* RSRC1: VGPRS=2 (8 VGPRs), SGPRS=1 (16 SGPRs), PRIORITY=0 */
    mqd[3] = (2 << 0) | (1 << 6);
    /* RSRC2: THREAD_GROUP_SIZE=63 (64 threads/wavefront) */
    mqd[4] = (63 << 0);
    WritePhys(ringPa, mqd, 2048);
    W(0x910C, 0); Sleep(10);
    W(0x910C, 1); Sleep(50);
    printf("MQD reloaded: PGM_LO=0x%08X ACTIVE=%d\n", R(0x8110), R(0x910C));

    W(0x34D0, 0x00010000);
    W(0x80E4, 1);
    W(0x80F0, 0); W(0x80F4, 0); W(0x80F8, 0);
    W(0x80FC, 1);
    W(0x80E0, 0x00000003);
    W(0x34D0, 0xE0000000);
    Sleep(200);
    printf("RSRC1: GRBM=0x%08X GRBM2=0x%08X FENCE=%llu\n",
        R(0x3260), R(0x3268), (ULONG64)R((UINT32)FENCE_ADDR));

    /* Method E: Write full MQD with RSRC1/2 + DISPATCH_INITIATOR loaded */
    printf("\n--- Method E: MQD with EVERYTHING + activation ---\n");
    UINT32 mqd2[512] = {0};
    mqd2[0] = 0;                                  /* header */
    mqd2[1] = (UINT32)(SHADER_ADDR >> 8);         /* PGM_LO */
    mqd2[2] = 0;                                   /* PGM_HI */
    mqd2[3] = (2 << 0) | (1 << 6) | (0 << 24);    /* RSRC1: 8 VGPR, 16 SGPR */
    mqd2[4] = (63 << 0);                           /* RSRC2: 64 threads */
    /* DW5 = COMPUTE_DIM_X = 0 (not in MQD) */
    /* DW6-7 = DIM_Y/Z = 0 */
    mqd2[8] = 0;                                   /* DISPATCH_INITIATOR = 0 */
    WritePhys(ringPa, mqd2, 2048);
    W(0x910C, 0); Sleep(10);
    W(0x910C, 1); Sleep(50);

    W(0x34D0, 0x00010000);
    W(0x80E4, 1);  /* DIM_X = 1 */
    W(0x80E0, 0x00000003);  /* DISPATCH */
    W(0x34D0, 0xE0000000);
    Sleep(200);
    UINT64 fence = 0;
    ReadPhys(FENCE_ADDR, &fence, 8);
    printf("Final: GRBM=0x%08X GRBM2=0x%08X FENCE=%llu\n",
        R(0x3260), R(0x3268), fence);

    /* Read all compute registers */
    printf("\n=== FINAL COMPUTE REGISTERS ===\n");
    for (UINT32 off = 0x80E0; off <= 0x8140; off += 4) {
        printf("  0x%04X = 0x%08X\n", off, R(off));
    }

    CloseHandle(gH);
    return 0;
}

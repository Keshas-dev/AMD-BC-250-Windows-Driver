/* smu-unlock-win.c — Windows port of rw-r-r-0644 bc250-smu-unlock (Q2 overflow -> 0x7B3C) via BAR5+0x38/0x3C */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
static HANDLE g_hDev=INVALID_HANDLE_VALUE;
#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
#define IOCTL_GPU_INIT  0x80000B80
typedef struct{ UINT32 RegisterOffset; UINT32 Value; } REG_IO;
static BOOL WriteReg(UINT32 o, UINT32 v){ REG_IO r; DWORD ret=0; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(g_hDev,IOCTL_GPU_WRITE,&r,sizeof(r),&r,sizeof(r),&ret,NULL); }
static UINT32 ReadReg(UINT32 o){ REG_IO r; DWORD ret=0; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(g_hDev,IOCTL_GPU_READ,&r,sizeof(r),&r,sizeof(r),&ret,NULL)) return r.Value; return 0xFFFFFFFF; }
static UINT32 SmnRead(UINT32 a){ WriteReg(0x38,a); ReadReg(0x38); return ReadReg(0x3C); }
static BOOL SmnWrite(UINT32 a, UINT32 v){ WriteReg(0x38,a); return WriteReg(0x3C,v); }
#define Q0_CMD 0x03B10A08
#define Q0_RSP 0x03B10A68
#define Q0_ARG 0x03B10A48
#define Q2_CMD 0x03B10528
#define Q2_RSP 0x03B10564
#define Q2_ARG 0x03B10998
#define Q3_CMD 0x03B10A20
#define Q3_RSP 0x03B10A80
#define Q3_ARG 0x03B10A88
static int WaitRsp(UINT32 addr,int ms){ for(int i=0;i<ms;i++){ UINT32 v=SmnRead(addr); if(v!=0 && v!=0xFFFFFFFF) return (int)v; Sleep(1);} return -1; }
static int SmuQ3(UINT16 msg, UINT32 arg, UINT32* resp){ if(SmnRead(Q3_RSP)==1) SmnWrite(Q3_RSP,0); SmnWrite(Q3_ARG,arg); SmnWrite(Q3_CMD,msg); int r=WaitRsp(Q3_RSP,2000); if(r<0) return -1; if(resp) *resp=SmnRead(Q3_ARG); return r; }
static int SmuQ2(UINT16 msg, UINT32 a0, UINT32 a1, UINT32 a2){ SmnWrite(Q2_RSP,0); SmnWrite(Q2_ARG,a0); SmnWrite(Q2_ARG+4,a1); SmnWrite(Q2_ARG+8,a2); SmnWrite(Q2_CMD,msg); return WaitRsp(Q2_RSP,2000); }

// Q2 msg 0x23 append
#define RING_BASE 0x18850
#define RING_ENTRY_SZ 16
#define RING_SUBQ_SLOTS 30
#define RING_CMD_TYPE(cmd,subq) ((cmd<<24)|subq)
#define TR_TABLE_PTR 0x19784
#define DBG_DISABLE 0x7B3C
static int subq4_cur_idx=0;
static void q2_0x23_append(UINT32 a0, UINT32 a1, UINT32 a2, UINT32 cmdtype){
    // Q2 0x23 takes 3 args: arg0=base, arg1, arg2, cmdtype in arg3? Use generic Q2 send with 3 args
    // Simplified: use SmuQ2 with msg 0x23 and pack args
    // Real impl needs to set SMN window page and write via mapped window - we mimic Python's q2_0x23_append
    // For Windows port, we use direct Q2 mailbox with msg 0x23 and args
    // This is stub — full transfer-engine needs DRAM phys addr
    SmnWrite(Q2_ARG, a0);
    SmnWrite(Q2_ARG+4, a1);
    SmnWrite(Q2_ARG+8, a2);
    SmnWrite(Q2_CMD, 0x23);
    WaitRsp(Q2_RSP, 500);
}
int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    g_hDev=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(g_hDev==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
    typedef struct{ UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize;} INIT_HW;
    INIT_HW ih; DWORD ret=0; ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=1;
    DeviceIoControl(g_hDev,IOCTL_GPU_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&ret,NULL);
    printf("GPU_ID 0x%08X\n",ReadReg(0x0000));
    // Check gate
    UINT32 probe=0; int r=SmuQ3(0x2A, 0x0005A870, &probe);
    printf("Q3 0x2A probe 0x5A870: r=%d resp=0x%08X %s\n",r,probe, (r==1?"OK":"REJECTED/closed"));
    if(r==1 && probe!=0xFFFFFFFF){ printf("SMU already unlocked (secure access open)\n"); return 0; }
    printf("SMU locked — need Q2 overflow unlock (not yet fully ported, needs DRAM phys)\n");
    printf("Next: implement P allocation + fake transfer table via Q2 0x0A\n");
    CloseHandle(g_hDev);
    return 0;
}

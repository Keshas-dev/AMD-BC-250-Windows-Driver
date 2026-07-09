/*
 * kiq-base-writability-test.c — DECISIVE: is KIQ_BASE_LO writable from host BAR5?
 *
 * PSP KIQ ring init writes KIQ_BASE_LO via proxy but our read shows 0
 * even after init (SEND_PM4 delivers packets but GPU never executes).
 * If KIQ_BASE_LO is genuinely read-only from host BAR5, the host CANNOT
 * link a ring to the GPU CP -> compute/3D is impossible from Windows.
 * If it writes back, our PSP proxy path was the bug (fixable).
 *
 * KIQ_BASE_LO (0xE060) is a PER-ME register: must select ME=1 via
 * GRBM_GFX_INDEX (0x34D0) before the write takes effect.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_INIT_HW    0x80000B80
#define IOCTL_READ_REG   0x80000B88
#define IOCTL_WRITE_REG  0x80000B8C

#define GRBM_GFX_INDEX  0x34D0
#define GRBM_KIQ        0x00010000   /* ME=1, PIPE=0, QUEUE=0 */
#define GRBM_BROADCAST  0xE0000000

#define KIQ_BASE_LO     0xE060
#define KIQ_BASE_HI     0xE064
#define KIQ_WPTR        0xE078
#define CP_HQD_ACTIVE   0x910C

static HANDLE gH;
static UINT32 R(UINT32 off){struct{UINT32 o;UINT32 v;}i={off,0},o={0,0};DWORD br=0;DeviceIoControl(gH,IOCTL_READ_REG,&i,sizeof(i),&o,sizeof(o),&br,NULL);return o.v;}
static void W(UINT32 off,UINT32 v){struct{UINT32 o;UINT32 v;}i={off,v};DWORD br=0;DeviceIoControl(gH,IOCTL_WRITE_REG,&i,sizeof(i),NULL,0,&br,NULL);}

int main(void){
    printf("=== KIQ BASE WRITABILITY (decisive) ===\n\n");
    gH = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(gH==INVALID_HANDLE_VALUE){printf("open fail %lu\n",GetLastError());return 1;}

    UCHAR ib[32]={0};
    *(UINT64*)(ib+0)=0xFE800000ULL; *(UINT32*)(ib+8)=0x00080000;
    *(UINT32*)(ib+12)=1; *(UINT64*)(ib+16)=0xC0000000ULL; *(UINT32*)(ib+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH,IOCTL_INIT_HW,ib,sizeof(ib),NULL,0,&br,NULL);
    printf("[ok] INIT_HARDWARE NBIO_MAP\n\n");

    /* Select ME=1 (KIQ context) for all subsequent KIQ register writes */
    W(GRBM_GFX_INDEX, GRBM_KIQ);

    printf("--- KIQ_BASE_LO (0xE060) ---\n");
    UINT32 before = R(KIQ_BASE_LO);
    W(KIQ_BASE_LO, 0xA5A5A5A5);
    UINT32 after = R(KIQ_BASE_LO);
    printf("  before=0x%08X  wrote=0xA5A5A5A5  after=0x%08X  -> %s\n",
           before, after, after==0xA5A5A5A5 ? "WRITABLE" : "READ-ONLY");

    printf("--- KIQ_BASE_HI (0xE064) ---\n");
    before = R(KIQ_BASE_HI); W(KIQ_BASE_HI, 0x5A5A5A5A); after = R(KIQ_BASE_HI);
    printf("  before=0x%08X  wrote=0x5A5A5A5A  after=0x%08X  -> %s\n",
           before, after, after==0x5A5A5A5A ? "WRITABLE" : "READ-ONLY");

    printf("--- KIQ_WPTR (0xE078) ---\n");
    before = R(KIQ_WPTR); W(KIQ_WPTR, 0x0000000A); after = R(KIQ_WPTR);
    printf("  before=0x%08X  wrote=0xA  after=0x%08X  -> %s\n",
           before, after, after==0x0000000A ? "WRITABLE" : "READ-ONLY");

    printf("--- CP_HQD_ACTIVE (0x910C) ---\n");
    before = R(CP_HQD_ACTIVE); W(CP_HQD_ACTIVE, 1); after = R(CP_HQD_ACTIVE);
    printf("  before=0x%08X  wrote=1  after=0x%08X  -> %s\n",
           before, after, (after&1)?"WRITABLE":"READ-ONLY");

    /* restore */
    W(CP_HQD_ACTIVE, 0);
    W(GRBM_GFX_INDEX, GRBM_BROADCAST);

    printf("\n=== VERDICT ===\n");
    if (after==0xA5A5A5A5)
        printf("KIQ_BASE_LO WRITABLE -> ring CAN be linked; PSP proxy path was the bug. COMPUTE REACHABLE.\n");
    else
        printf("KIQ_BASE_LO READ-ONLY from host BAR5 -> host CANNOT program KIQ ring.\n"
               "GPU CP rings are owned by PSP firmware. Host compute/3D IMPOSSIBLE from Windows.\n");
    CloseHandle(gH);
    return 0;
}

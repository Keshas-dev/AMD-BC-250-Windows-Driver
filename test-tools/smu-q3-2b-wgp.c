/* smu-q3-2b-wgp.c — Q3 0x2B mem64 SMN write to SPI_PG 0x5C3C (try via SMU since unlocked) */
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
static BOOL SmnWrite(UINT32 a, UINT32 v){ WriteReg(0x38,a); ReadReg(0x38); return WriteReg(0x3C,v); }
#define Q0_CMD 0x03B10A08
#define Q0_RSP 0x03B10A68
#define Q0_ARG 0x03B10A48
#define Q3_CMD 0x03B10A20
#define Q3_RSP 0x03B10A80
#define Q3_ARG 0x03B10A88
static int WaitRsp(UINT32 addr,int ms){ for(int i=0;i<ms;i++){ UINT32 v=SmnRead(addr); if(v==1||v==0xFF||v==0xFE||v==0xFD||v==0xFC) return (int)v; Sleep(1);} return -100; }
static int SmuQ0(UINT16 msg, UINT32 arg, UINT32* out){ SmnWrite(Q0_RSP,0); SmnWrite(Q0_ARG,arg); SmnWrite(Q0_CMD,msg); int r=WaitRsp(Q0_RSP,2000); if(r<0) return r; if(out) *out=SmnRead(Q0_ARG); return r; }
static int SmuQ3(UINT16 msg, UINT32 arg, UINT32* out){ SmnWrite(Q3_RSP,0); SmnWrite(Q3_ARG,arg); SmnWrite(Q3_CMD,msg); int r=WaitRsp(Q3_RSP,2000); if(r<0) return r; if(out) *out=SmnRead(Q3_ARG); return r; }
/* Q3 0x2B is mem64 write: takes 2 args — arg0=SMN addr, arg1=value; ack in ARG */
static int SmuQ3Mem64Write(UINT32 smn, UINT32 value){
    SmnWrite(Q3_RSP,0);
    SmnWrite(Q3_ARG, smn);
    SmnWrite(Q3_ARG+4, value);
    SmnWrite(Q3_CMD, 0x2B);
    return WaitRsp(Q3_RSP, 2000);
}
/* Q3 0x2A is mem64 read: takes 1 arg — SMN addr; return in ARG */
static int SmuQ3Mem64Read(UINT32 smn, UINT32* out){
    SmnWrite(Q3_RSP,0);
    SmnWrite(Q3_ARG, smn);
    SmnWrite(Q3_CMD, 0x2A);
    int r=WaitRsp(Q3_RSP, 2000);
    if(r!=1) return r;
    if(out) *out=SmnRead(Q3_ARG);
    return 1;
}
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 g_hDev=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(g_hDev==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 typedef struct{ UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize;} INIT_HW;
 INIT_HW ih; DWORD ret=0; ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=1;
 DeviceIoControl(g_hDev,IOCTL_GPU_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&ret,NULL);
 printf("GPU_ID 0x%08X\n",ReadReg(0x0000));
 /* Sanity: Q3 0x2A read of core mask 0x5A870 */
 UINT32 probe=0; int rs=SmuQ3(0x2A, 0x0005A870, &probe);
 printf("Q3 0x2A 0x5A870 r=0x%02X resp=0x%08X (expect r=1)\n",rs,probe);
 if(rs!=1){printf("SMU secure not open — abort\n");return 1;}
 /* Baseline: read current SPI_PG 0x5C3C via BAR5 (raw) and via mem64 window */
 UINT32 spi_b5=ReadReg(0x5C3C);
 UINT32 spi_m64=0; SmuQ3Mem64Read(0x0005C3C, &spi_m64);
 printf("Baseline SPI_PG(0x5C3C): BAR5=0x%08X  mem64=0x%08X\n", spi_b5, spi_m64);
 /* Disable GFXOFF+CG+PG via Q2 0x06 0x1C */
 if(SmnRead(0x03B10564)==1) SmnWrite(0x03B10564,0);
 SmnWrite(0x03B10998, 0x1C); SmnWrite(0x03B10998+4, 0); SmnWrite(0x03B10528, 0x06); WaitRsp(0x03B10564, 2000);
 UINT32 feat=0; SmuQ0(0x3D,0,&feat); printf("Features 0x%08X\n",feat);
 /* Per-bank GRBM select via raw BAR5 (works for SPI_PG per duggasco) */
 /* Try Q3 0x2B write to SPI_PG SMN candidate addresses (full 32-bit) */
 UINT32 spi_addr=0x5C3C; /* raw SMN, no masking */
 UINT32 targets[]={ spi_addr, 0x0005C3C, 0x0115C3C, 0x02405C3C, 0x02402C3C };
 for(int i=0;i<sizeof(targets)/sizeof(targets[0]);i++){
   printf("\n=== Target SMN 0x%08X ===\n", targets[i]);
   /* Read pre */
   UINT32 pre=0; SmuQ3Mem64Read(targets[i], &pre);
   printf("  mem64 read pre  = 0x%08X\n", pre);
   /* Write 0x1F via Q3 0x2B */
   int wr=SmuQ3Mem64Write(targets[i], 0x1F);
   printf("  Q3 0x2B write 0x1F -> r=0x%02X\n", wr);
   Sleep(50);
   /* Read post both mem64 and BAR5 */
   UINT32 post_m64=0; SmuQ3Mem64Read(targets[i], &post_m64);
   UINT32 post_b5=ReadReg(0x5C3C);
   printf("  mem64 read post = 0x%08X\n", post_m64);
   printf("  BAR5 0x5C3C post = 0x%08X\n", post_b5);
   if((post_b5 & 0x1F)==0x1F){printf("*** SPI_PG UNLOCKED via SMN 0x%08X ***\n",targets[i]); break;}
 }
 /* Reset GRBM and final Q0 0x1E */
 WriteReg(0x34D0, 0xE0000000);
 UINT32 wgp=0; SmuQ0(0x1E,0,&wgp);
 printf("\nQueryActiveWgp r=%d val=%u\n",1,wgp);
 return 0;
}

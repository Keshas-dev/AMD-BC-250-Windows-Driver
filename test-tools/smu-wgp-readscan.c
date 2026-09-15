/* smu-wgp-readscan.c — find SMN alias of BAR5 0x5C3C (SPI_PG) via Q3 0x2A read scan */
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
#define Q3_CMD 0x03B10A20
#define Q3_RSP 0x03B10A80
#define Q3_ARG 0x03B10A88
static int WaitRsp(int ms){ for(int i=0;i<ms;i++){ UINT32 v=SmnRead(Q3_RSP); if(v!=0 && v!=0xFFFFFFFF) return (int)v; Sleep(1);} return -1; }
static int SmuQ3Read(UINT32 addr, UINT32* out){ if(SmnRead(Q3_RSP)==1) SmnWrite(Q3_RSP,0); SmnWrite(Q3_ARG,addr); SmnWrite(Q3_CMD,0x2A); int r=WaitRsp(2000); if(r<0||r!=1) return r; if(out) *out=SmnRead(Q3_ARG); return 1; }
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 g_hDev=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(g_hDev==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 typedef struct{ UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize;} INIT_HW;
 INIT_HW ih; DWORD ret=0; ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=1;
 DeviceIoControl(g_hDev,IOCTL_GPU_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&ret,NULL);
 UINT32 probe=0; SmuQ3Read(0x0005A870,&probe);
 printf("SMU secure probe r=%d resp=0x%08X (need r=1)\n",1,probe);
 if(probe==0xFFFFFFFF){printf("SMU locked\n");return 1;}
 printf("SMU UNLOCKED. Scanning GC SMN slice 0x02402C00-0x02403000 step 0x4 (16KB = 4K entries)...\n");
 // SPI_PG MASK signature: 0x5C3C read=0 (gated) on BC-250. We look for an address where Q3 0x2A read returns non-zero or 0x0 with stable pattern, and post-write of 0x1F via Q3 0x2B changes BAR5 0x5C3C.
 // But Q3 0x2A is read with arg=SMN addr, returns 4 bytes. Try sparse scan first.
 UINT32 saved_spi=ReadReg(0x5C3C);
 printf("BAR5 SPI_PG baseline 0x%08X\n",saved_spi);
 int hits=0;
 // First coarse: 0x100 step to find live chunks
 for(UINT32 smn=0x02402C00; smn<0x02403000; smn+=0x100){
   UINT32 val=0xDEADBEEF;
   int r=SmuQ3Read(smn,&val);
   if(r==1 && val!=0xDEADBEEF && val!=0xFFFFFFFF){
     printf("  LIVE SMN 0x%08X r=1 val=0x%08X\n",smn,val);
     // fine scan this 0x100 chunk
     for(UINT32 off=0; off<0x100; off+=0x4){
       UINT32 v2=0; SmuQ3Read(smn+off,&v2);
       if(v2!=0 && v2!=0xFFFFFFFF) printf("    0x%08X = 0x%08X\n",smn+off,v2);
     }
     hits++;
   }
 }
 printf("Total non-FAIL/DEAD reads in slice: %d\n",hits);
 // Now try writing SPI_PG-like 0x1F via Q3 0x2B at each read-success address and check BAR5
 printf("\nAttempt Q3 0x2B (write) at found addresses with value 0x1F and check BAR5 SPI_PG...\n");
 // Q3 0x2B may be different msg - try both
 for(int msg=0x2B; msg<=0x2C; msg++){
   for(UINT32 smn=0x02402C00; smn<0x02403000; smn+=0x4){
     SmnWrite(Q3_RSP,0);
     SmnWrite(Q3_ARG,smn);
     SmnWrite(Q3_ARG+4,0x1F);
     SmnWrite(Q3_CMD,msg);
     int r=WaitRsp(200);
     if(r==1){
       UINT32 spi=ReadReg(0x5C3C);
       if((spi & 0x1F)==0x1F){ printf("*** UNLOCKED msg=0x%X SMN=0x%08X SPI_PG=0x%08X ***\n",msg,smn,spi); return 0; }
     }
   }
   printf("msg 0x%X done, no hit\n",msg);
 }
 printf("No SPI_PG alias found via Q3 0x2A/0x2B in GC slice\n");
 return 0;
}

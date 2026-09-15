/* smu-wgp-scan.c — after SMU unlock, scan SMN for SPI_PG alias and try Q3 0x98 WGP unlock */
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
static int WaitRsp(UINT32 addr,int ms){ for(int i=0;i<ms;i++){ UINT32 v=SmnRead(addr); if(v!=0 && v!=0xFFFFFFFF) return (int)v; Sleep(1);} return -1; }
static int SmuQ3(UINT16 msg, UINT32 arg, UINT32* resp){ if(SmnRead(Q3_RSP)==1) SmnWrite(Q3_RSP,0); SmnWrite(Q3_ARG,arg); SmnWrite(Q3_CMD,msg); int r=WaitRsp(Q3_RSP,2000); if(r<0) return -1; if(resp) *resp=SmnRead(Q3_ARG); return r; }
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 g_hDev=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(g_hDev==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 typedef struct{ UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize;} INIT_HW;
 INIT_HW ih; DWORD ret=0; ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=1;
 DeviceIoControl(g_hDev,IOCTL_GPU_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&ret,NULL);
 printf("GPU_ID 0x%08X\n",ReadReg(0x0000));
 UINT32 probe=0; int r=SmuQ3(0x2A, 0x0005A870, &probe);
 printf("SMU secure probe Q3 0x2A 0x5A870: r=%d resp=0x%08X %s\n",r,probe,(r==1?"UNLOCKED":"LOCKED"));
 if(r!=1){ printf("SMU still locked, abort\n"); return 1; }
 // Try Q3 0x98 to SPI_PG SMN alias candidates
 // SPI_PG BAR5 0x5C3C -> try SMN 0x02402C00+0x5C3C? Not known. Brute scan 0x02402C00-0x02403000 step 0x100 for GC slice
 printf("\nTrying Q3 0x98 (write 0xFF) to GC SMN slice candidates for SPI_PG...\n");
 UINT32 candidates[]={0x02402C3C,0x02402C00+0x5C3C,0x00005C3C,0x01105C3C,0x01F05C3C};
 for(int i=0;i<sizeof(candidates)/sizeof(candidates[0]);i++){
   UINT32 addr=candidates[i];
   printf(" Q3 0x98 -> SMN 0x%08X ... ",addr);
   int rr=SmuQ3(0x98, addr, NULL);
   printf("r=%d\n",rr);
   Sleep(100);
   // Check BAR5 SPI_PG after
   UINT32 spi=ReadReg(0x5C3C);
   printf("  BAR5 SPI_PG now 0x%08X\n",spi);
   if((spi & 0x1F)==0x1F){ printf("*** WGP UNLOCKED via SMN 0x%08X ***\n",addr); break; }
 }
 // Also try direct SMN write via Q3 0x2B (if secure)
 printf("\nTrying direct SMN write via Q3 0x2B (if supported) to 0x02402C3C = 0x1F...\n");
 // Q3 0x2B is SMN write via window, but we can try
 int wr=SmuQ3(0x2B, 0x02402C3C, NULL); printf(" Q3 0x2B r=%d\n",wr);
 Sleep(100);
 printf(" SPI_PG after 0x2B: 0x%08X\n", ReadReg(0x5C3C));
 return 0;
}

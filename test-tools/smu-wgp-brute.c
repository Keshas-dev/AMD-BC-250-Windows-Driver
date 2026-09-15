/* smu-wgp-brute.c — brute scan GC SMN slice for SPI_PG alias via Q3 0x98 */
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
static int SmuQ3(UINT16 msg, UINT32 arg){ if(SmnRead(Q3_RSP)==1) SmnWrite(Q3_RSP,0); SmnWrite(Q3_ARG,arg); SmnWrite(Q3_CMD,msg); return WaitRsp(Q3_RSP,2000); }
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 g_hDev=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(g_hDev==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 typedef struct{ UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize;} INIT_HW;
 INIT_HW ih; DWORD ret=0; ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=1;
 DeviceIoControl(g_hDev,IOCTL_GPU_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&ret,NULL);
 printf("GPU_ID 0x%08X SMU unlocked probe...\n",ReadReg(0x0000));
 UINT32 probe=0; SmnWrite(Q3_RSP,0); SmnWrite(Q3_ARG,0x0005A870); SmnWrite(Q3_CMD,0x2A); int r=WaitRsp(Q3_RSP,2000); if(r>0) probe=SmnRead(Q3_ARG);
 printf("Q3 0x2A probe r=%d resp=0x%08X\n",r,probe);
 if(r!=1){printf("SMU locked\n");return 1;}
 printf("Brute scan GC SMN 0x02402C00-0x02403000 step 0x4 via Q3 0x98 (write 0xFF) and check BAR5 SPI_PG...\n");
 for(UINT32 smn=0x02402C00; smn<0x02403000; smn+=0x4){
   int rr=SmuQ3(0x98, smn);
   if(rr!=1){ printf(" Q3 0x98 SMN 0x%08X REJECTED r=%d -> SMU relocked, abort\n",smn,rr); return 1; }
   UINT32 spi=ReadReg(0x5C3C);
   if((spi & 0x1F)==0x1F){
     printf("FOUND SMN 0x%08X -> SPI_PG 0x%08X after Q3 0x98 r=%d\n",smn,spi,rr);
     return 0;
   }
   if((smn & 0x3FF)==0) printf("  scanned 0x%08X spi 0x%08X\n",smn,spi);
 }
 printf("No alias found in GC slice\n");
 return 0;
}

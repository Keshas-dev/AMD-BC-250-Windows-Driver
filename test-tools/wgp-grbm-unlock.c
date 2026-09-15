/* wgp-grbm-unlock.c — per-bank GRBM write per duggasco bc250-40cu patch */
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
static int WaitRspDone(UINT32 addr,int ms){
    for(int i=0;i<ms;i++){
        UINT32 v=SmnRead(addr);
        if(v==1||v==0xFF||v==0xFE||v==0xFD||v==0xFC) return (int)v;
        Sleep(1);
    }
    return -100;
}
static int SmuQ0(UINT16 msg, UINT32 arg, UINT32* out){
    SmnWrite(Q0_RSP,0); SmnWrite(Q0_ARG,arg); SmnWrite(Q0_CMD,msg);
    int r=WaitRspDone(Q0_RSP,2000);
    if(r<0) return r;
    if(out) *out=SmnRead(Q0_ARG);
    return r;
}
static int SmuQ3(UINT16 msg, UINT32 arg, UINT32* out){
    SmnWrite(Q3_RSP,0); SmnWrite(Q3_ARG,arg); SmnWrite(Q3_CMD,msg);
    int r=WaitRspDone(Q3_RSP,2000);
    if(r<0) return r;
    if(out) *out=SmnRead(Q3_ARG);
    return r;
}
static UINT32 grbm_select_bc250(int se, int sh){
    UINT32 v=0;
    v |= (se & 0x3F) << 16;
    v |= (sh & 0x3F) << 8;
    v |= 0x3F;                 /* instance = 0x3F (broad) */
    v |= (1u<<24);             /* INSTANCE_BROADCAST */
    if(sh==0xFF) v |= (1u<<25);
    if(se==0xFF) v |= (1u<<26);
    return v;
}
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 g_hDev=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(g_hDev==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 typedef struct{ UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize;} INIT_HW;
 INIT_HW ih; DWORD ret=0; ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=1;
 DeviceIoControl(g_hDev,IOCTL_GPU_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&ret,NULL);
 printf("GPU_ID 0x%08X\n",ReadReg(0x0000));
 /* SMU alive */
 SmnWrite(Q3_RSP,0); SmnWrite(Q3_ARG,123); SmnWrite(Q3_CMD,1);
 int alive=0; for(int i=0;i<500;i++){UINT32 st=SmnRead(Q3_RSP);if(st==1){alive=1;break;}if(st==0xFF)break;Sleep(1);}
 printf("SMU Q3 alive=%d resp=%u\n",alive,SmnRead(Q3_ARG));
 if(!alive){printf("SMU dead\n");return 1;}
 /* SMU secure access probe Q3 0x2A -> mem64 read */
 UINT32 probe=0; int rs=SmuQ3(0x2A, 0x0005A870, &probe);
 printf("SMU secure Q3 0x2A 0x5A870: r=0x%02X resp=0x%08X %s\n",rs,probe,(rs==1?"UNLOCKED":"locked"));
 if(rs!=1){printf("SMU secure access not open\n");return 1;}
 /* Q0 0x3D GetEnabledSmuFeatures */
 UINT32 feat_before=0,feat_after=0;
 SmuQ0(0x3D,0,&feat_before);
 printf("Features before 0x%08X\n",feat_before);
 /* Q2 0x06 disable bits 2|3|4 (GFXOFF+CG+PG) */
 if(SmnRead(0x03B10564)==1) SmnWrite(0x03B10564,0);
 SmnWrite(0x03B10998, 0x1C);
 SmnWrite(0x03B10998+4, 0);
 SmnWrite(0x03B10528, 0x06);
 WaitRspDone(0x03B10564, 2000);
 SmuQ0(0x3D,0,&feat_after);
 printf("Features after  0x%08X (expect 0x%08X)\n",feat_after, feat_before & ~0x1C);
 /* Per-bank write following duggasco patch:
  *   for each (se,sh) in {0,1}x{0,1}:
  *     select_se_sh(se,sh,0xffffffff,0)
  *     CC=0, SPI=0x1F, RLC=0x1F
  */
 for(int se=0;se<2;se++) for(int sh=0;sh<2;sh++){
   UINT32 grbm=grbm_select_bc250(se,sh);
   WriteReg(0x34D0,grbm);
   UINT32 sel=ReadReg(0x34D0);
   UINT32 cc_o=ReadReg(0x9C1C);
   UINT32 spi_o=ReadReg(0x5C3C);
   UINT32 rlc_o=ReadReg(0x3D64);
   WriteReg(0x9C1C, 0);
   WriteReg(0x5C3C, 0x1F);
   WriteReg(0x3D64, 0x1F);
   UINT32 cc_a=ReadReg(0x9C1C);
   UINT32 spi_a=ReadReg(0x5C3C);
   UINT32 rlc_a=ReadReg(0x3D64);
   printf("SE%d/SH%d grbm=0x%08X sel=0x%08X | CC 0x%08X->0x%08X | SPI 0x%08X->0x%08X | RLC 0x%08X->0x%08X\n",
     se,sh,grbm,sel,cc_o,cc_a,spi_o,spi_a,rlc_o,rlc_a);
   if(spi_a==0x1F){printf("*** SPI_PG UNLOCKED on SE%d/SH%d ***\n",se,sh);}
 }
 /* Reset GRBM to all-broadcast */
 WriteReg(0x34D0, grbm_select_bc250(0xFF,0xFF));
 /* QueryActiveWgp (Q0 0x1E) */
 UINT32 wgp=0;
 SmuQ0(0x1E,0,&wgp);
 printf("QueryActiveWgp=%u\n",wgp);
 /* Scratch and GRBM status */
 printf("Scratch(0x32D4)=0x%08X  GRBM_STATUS(0x3260)=0x%08X\n",ReadReg(0x32D4),ReadReg(0x3260));
 return 0;
}

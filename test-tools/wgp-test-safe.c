/* wgp-test-safe.c â€” safe WGP test using aligned stack buffer, no BSOD */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#define IOCTL_READ  0x80000B88
#define IOCTL_WRITE 0x80000B8C
#define IOCTL_INIT  0x80000B80
#define IOCTL_UNLOCK_40CU 0x80000980
typedef struct{ uint32_t RegisterOffset; uint32_t Value; } REG_IO;
typedef struct{ uint64_t MmioPhysicalBase; uint32_t MmioSize; uint32_t Flags; uint64_t FbPhysicalBase; uint32_t FbSize; } INIT_HW;
static HANDLE g_h=INVALID_HANDLE_VALUE;
static DWORD g_dwLastErr=0;
static BOOL W32(uint32_t o, uint32_t v){
    REG_IO r; DWORD br=0; r.RegisterOffset=o; r.Value=v;
    BOOL ok=DeviceIoControl(g_h,IOCTL_WRITE,&r,sizeof(r),&r,sizeof(r),&br,NULL);
    g_dwLastErr=GetLastError();
    if(!ok) printf("  [W32 FAIL o=0x%X v=0x%X gle=%lu]\n",o,v,g_dwLastErr);
    return ok;
}
static uint32_t R32(uint32_t o){
    REG_IO r; DWORD br=0; r.RegisterOffset=o; r.Value=0;
    if(!DeviceIoControl(g_h,IOCTL_READ,&r,sizeof(r),&r,sizeof(r),&br,NULL)){
        g_dwLastErr=GetLastError();
        printf("  [R32 FAIL o=0x%X gle=%lu]\n",o,g_dwLastErr);
        return 0xDEADBEEF;
    }
    return r.Value;
}
static BOOL SmnR(uint32_t addr, uint32_t* out){
    if(!W32(0x38,addr)) return FALSE;
    R32(0x38); /* flush */
    *out=R32(0x3C);
    return TRUE;
}
static BOOL SmnW(uint32_t addr, uint32_t v){
    W32(0x38,addr); R32(0x38); return W32(0x3C,v);
}
static int WaitRsp(uint32_t addr, int ms){
    for(int i=0;i<ms;i++){
        uint32_t v=0; SmnR(addr,&v);
        if(v==1||v==0xFF||v==0xFE||v==0xFD||v==0xFC) return (int)v;
        Sleep(1);
    }
    return -100;
}
static int SmuQ3(uint16_t msg, uint32_t arg, uint32_t* out){
    SmnW(0x03B10A80,0);
    SmnW(0x03B10A88,arg);
    SmnW(0x03B10A20,msg);
    int r=WaitRsp(0x03B10A80,2000);
    if(r<0) return r;
    if(out) SmnR(0x03B10A88,out);
    return r;
}
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 g_h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(g_h==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 INIT_HW ih; DWORD br=0; ZeroMemory(&ih,sizeof(ih));
 ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=1;
 if(!DeviceIoControl(g_h,IOCTL_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){
    printf("INIT FAIL gle=%lu\n",GetLastError()); CloseHandle(g_h); return 1;
 }
 printf("GPU_ID 0x%08X\n",R32(0x0000));
 /* SMU alive test */
 uint32_t probe=0; int rs=SmuQ3(0x2A,0x5A870,&probe);
 printf("Q3 0x2A 0x5A870: r=0x%02X resp=0x%08X\n",rs,probe);
 if(rs!=1){printf("SMU not unlocked\n");CloseHandle(g_h);return 1;}
 /* Baseline */
 uint32_t spi_b=R32(0x5C3C), cc_b=R32(0x9C1C), grbm_b=R32(0x34D0);
 printf("BEFORE: SPI_PG=0x%08X CC=0x%08X GRBM=0x%08X\n",spi_b,cc_b,grbm_b);
 /* Per-bank GRBM (gfx10.1): SA=bit8, SE=bit16 */
 static const uint32_t BANKS[4]={ 0x00000000, 0x00000100, 0x00010000, 0x00010100 };
 printf("WGP unlock per-bank (gfx10.1 GRBM)...\n");
 for(int b=0;b<4;b++){
   W32(0x34D0,BANKS[b]);
   W32(0x9C1C,0); /* CC=0 */
   W32(0x5C3C,0x1F); /* SPI=5WGPs */
   W32(0x3D64,0x1F); /* RLC */
   uint32_t spi=R32(0x5C3C), cc=R32(0x9C1C), rlc=R32(0x3D64);
   printf("  Bank%d grbm=0x%08X: SPI 0x%08X CC 0x%08X RLC 0x%08X\n",b,BANKS[b],spi,cc,rlc);
   if((spi & 0x1F)==0x1F){printf("    *** SPI UNLOCKED ***\n"); break; }
 }
 /* Reset GRBM to all-broadcast */
 W32(0x34D0, 0xE0000000);
 uint32_t spi_a=R32(0x5C3C), cc_a=R32(0x9C1C), grbm_a=R32(0x34D0);
 printf("AFTER:  SPI_PG=0x%08X CC=0x%08X GRBM=0x%08X\n",spi_a,cc_a,grbm_a);
 /* QueryActiveWgp */
 uint32_t wgp=0; int rw=SmuQ3(0x2A, 0x5A870, &wgp);
 printf("Q3 0x2A 0x5A870 again: r=0x%02X v=0x%08X\n",rw,wgp);
 /* Q0 0x1E (different mailbox) */
 SmnW(0x03B10A68,0);
 SmnW(0x03B10A48,0);
 SmnW(0x03B10A08,0x1E);
 int r2=WaitRsp(0x03B10A68,2000);
 uint32_t arg=0; SmnR(0x03B10A48,&arg);
 printf("Q0 0x1E: r=0x%02X arg=0x%08X (WGP count)\n",r2,arg);
 CloseHandle(g_h);
 return 0;
}


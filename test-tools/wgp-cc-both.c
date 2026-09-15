/* wgp-cc-both.c â€” Test both duggasco CC=0 AND UEFI CC=0xFFE00000 values
 * with correct gfx10.1 per-bank GRBM, with SMU secure unlock */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#define IOCTL_R 0x80000B88
#define IOCTL_W 0x80000B8C
#define IOCTL_I 0x80000B80
typedef struct{ uint32_t off; uint32_t val; } RIO;
typedef struct{ uint64_t base; uint32_t sz; uint32_t flags; uint64_t fb; uint32_t fbsz; } IHW;
static HANDLE g_h;
static DWORD g_le;
static BOOL W32(uint32_t o,uint32_t v){ RIO r={o,v}; DWORD br; BOOL ok=DeviceIoControl(g_h,IOCTL_W,&r,sizeof(r),&r,sizeof(r),&br,NULL); g_le=GetLastError(); if(!ok) printf(" W32[%X]=%X gle=%lu\n",o,v,g_le); return ok; }
static uint32_t R32(uint32_t o){ RIO r={o,0}; DWORD br; if(!DeviceIoControl(g_h,IOCTL_R,&r,sizeof(r),&r,sizeof(r),&br,NULL)){g_le=GetLastError();return 0xFFFFFFFF;} return r.val; }
static uint32_t SMR(uint32_t a){ W32(0x38,a); R32(0x38); return R32(0x3C); }
static BOOL SMW(uint32_t a,uint32_t v){ W32(0x38,a); R32(0x38); return W32(0x3C,v); }
static int WaitRsp(uint32_t a,int ms){ for(int i=0;i<ms;i++){uint32_t v=SMR(a); if(v==1||v==0xFF||v==0xFE||v==0xFD||v==0xFC) return (int)v; Sleep(1);} return -100; }
static int Q3(uint16_t m,uint32_t a,uint32_t*o){ SMW(0x03B10A80,0); SMW(0x03B10A88,a); SMW(0x03B10A20,m); int r=WaitRsp(0x03B10A80,2000); if(r==1&&o) *o=SMR(0x03B10A88); return r; }
static int Q0(uint16_t m,uint32_t a,uint32_t*o){ SMW(0x03B10A68,0); SMW(0x03B10A48,a); SMW(0x03B10A08,m); int r=WaitRsp(0x03B10A68,2000); if(r==1&&o) *o=SMR(0x03B10A48); return r; }
static int Q2(uint16_t m,uint32_t a,uint32_t ah){ SMW(0x03B10564,0); SMW(0x03B10998,a); SMW(0x03B1099C,ah); SMW(0x03B10528,m); return WaitRsp(0x03B10564,2000); }

int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 g_h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(g_h==INVALID_HANDLE_VALUE){printf("FAIL %lu\n",GetLastError());return 1;}
 IHW ih={0xFE800000ULL,0x80000,1,0,0}; DWORD br; DeviceIoControl(g_h,IOCTL_I,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL);
 printf("GPU_ID 0x%08X\n",R32(0));
 /* SMU secure check */
 uint32_t v=0; int rs=Q3(0x2A,0x5A870,&v);
 printf("Q3 0x2A 0x5A870: r=%d v=0x%X\n",rs,v);
 if(rs!=1){printf("SMU not unlocked\n");return 1;}
 /* Disable GFXOFF+CG+PG */
 Q2(0x06, 0x1C, 0);
 uint32_t feat=0; Q0(0x3D,0,&feat); printf("Features 0x%08X\n",feat);
 /* Per-bank GRBM with both CC values */
 static const uint32_t BANKS[4]={0x0, 0x100, 0x10000, 0x10100};
 static const struct{uint32_t cc; const char* name;} CC_TESTS[]={
  {0x0,       "duggasco CC=0"},
  {0xFFE00000,"UEFI+AGENTS CC=0xFFE00000"},
  {0xFFF80000,"stock 24CU"},
  {0xFFE00000,"40CU unlock (UEFI)"},
 };
 for(int t=0;t<4;t++){
   printf("\n=== Test: %s (CC=0x%X) ===\n", CC_TESTS[t].name, CC_TESTS[t].cc);
   for(int b=0;b<4;b++){
     W32(0x34D0, BANKS[b]);
     W32(0x9C1C, CC_TESTS[t].cc);
     W32(0x5C3C, 0x1F);
     W32(0x3D64, 0x1F);
     uint32_t spi=R32(0x5C3C), cc=R32(0x9C1C), rlc=R32(0x3D64);
     printf("  Bank%d grbm=0x%08X: SPI 0x%08X CC 0x%08X RLC 0x%08X\n",b,BANKS[b],spi,cc,rlc);
     if((spi&0x1F)==0x1F){printf("  *** SPI UNLOCKED ***\n"); goto done; }
   }
 }
done:
 W32(0x34D0, 0xE0000000);
 uint32_t wgp=0; Q0(0x1E,0,&wgp);
 printf("\nFinal: SPI=0x%X CC=0x%X RLC=0x%X WGP=%u\n", R32(0x5C3C),R32(0x9C1C),R32(0x3D64),wgp);
 return 0;
}


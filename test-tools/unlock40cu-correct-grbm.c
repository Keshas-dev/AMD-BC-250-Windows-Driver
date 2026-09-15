/* unlock40cu-correct-grbm.c — IOCTL 0x80000980 with gfx10.1 GRBM layout (SA=bit8, SE=bit16) */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#define IOCTL_UNLOCK_40CU 0x80000980
#define IOCTL_GET_CU_STATUS 0x80000984
typedef struct{ uint32_t RegisterOffset; uint32_t Value; } REG_IO;
typedef struct{ uint32_t MmioPhysicalBase; uint32_t MmioSize; uint32_t Flags; uint64_t FbPhysicalBase; uint32_t FbSize; } INIT_HW;
static HANDLE g_h=INVALID_HANDLE_VALUE;
static uint32_t smn_r(uint32_t a){ REG_IO r={0x38,a}; DWORD br; DeviceIoControl(g_h,0x80000B88,&r,sizeof(r),&r,sizeof(br),&br,NULL); return r.Value; }
static uint32_t smn_r2(uint32_t a){ REG_IO r={0x38,a}; DWORD br; DeviceIoControl(g_h,0x80000B88,&r,sizeof(r),&r,sizeof(br),&br,NULL); return r.Value; }
static uint32_t r32(uint32_t o){ REG_IO r={o,0}; DWORD br; DeviceIoControl(g_h,0x80000B88,&r,sizeof(r),&r,sizeof(br),&br,NULL); return r.Value; }
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 g_h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(g_h==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 INIT_HW ih={0xFE800000,0x80000,1,0,0}; DWORD br;
 DeviceIoControl(g_h,0x80000B80,&ih,sizeof(ih),&ih,sizeof(br),&br,NULL);
 printf("GPU_ID 0x%08X\n",r32(0));
 /* Verify SMU alive */
 uint32_t v=0; REG_IO r={0x03B10A20,0}; DeviceIoControl(g_h,0x80000B8C,&r,sizeof(r),&r,sizeof(br),&br,NULL);
 /* Q3 0x2A probe */
 REG_IO r2={0x03B10A20,123}; DeviceIoControl(g_h,0x80000B8C,&r2,sizeof(r2),&r2,sizeof(br),&br,NULL);
 Sleep(10);
 uint32_t rsp=smn_r(0x03B10A80);
 printf("Q3 alive rsp=0x%08X\n",rsp);
 /* Baseline */
 uint32_t spi_b=r32(0x5C3C), cc_b=r32(0x9C1C), grbm=r32(0x34D0);
 printf("Before: SPI_PG=0x%08X CC=0x%08X GRBM=0x%08X\n",spi_b,cc_b,grbm);
 /* Disable GFXOFF+CG+PG first (Q2 0x06 0x1C) */
 REG_IO q2a={0x03B10564,0}; DeviceIoControl(g_h,0x80000B8C,&q2a,sizeof(q2a),&q2a,sizeof(br),&br,NULL);
 REG_IO q2b={0x03B10998,0x1C}; DeviceIoControl(g_h,0x80000B8C,&q2b,sizeof(q2b),&q2b,sizeof(br),&br,NULL);
 REG_IO q2c={0x03B1099C,0}; DeviceIoControl(g_h,0x80000B8C,&q2c,sizeof(q2c),&q2c,sizeof(br),&br,NULL);
 REG_IO q2d={0x03B10528,0x06}; DeviceIoControl(g_h,0x80000B8C,&q2d,sizeof(q2d),&q2d,sizeof(br),&br,NULL);
 Sleep(50);
 /* Now do the WGP unlock with gfx10.1 GRBM per-bank */
 /* gfx10.1: INSTANCE bits[7:0], SA bits[15:8], SE bits[23:16], broadcast bits 29,30,31 */
 static const uint32_t BANKS[4]={ 0x00000000, 0x00000100, 0x00010000, 0x00010100 };
 printf("WGP unlock (gfx10.1 GRBM, banks 0,1,2,3 with SA/SE bits 8/16)...\n");
 for(int b=0;b<4;b++){
   REG_IO gs={0x34D0,BANKS[b]}; DeviceIoControl(g_h,0x80000B8C,&gs,sizeof(gs),&gs,sizeof(br),&br,NULL);
   /* CC=0 (clear harvest mask) */
   REG_IO cc={0x9C1C,0}; DeviceIoControl(g_h,0x80000B8C,&cc,sizeof(cc),&cc,sizeof(br),&br,NULL);
   /* SPI=0x1F (5 WGPs) */
   REG_IO sp={0x5C3C,0x1F}; DeviceIoControl(g_h,0x80000B8C,&sp,sizeof(sp),&sp,sizeof(br),&br,NULL);
   /* RLC=0x1F */
   REG_IO rl={0x3D64,0x1F}; DeviceIoControl(g_h,0x80000B8C,&rl,sizeof(rl),&rl,sizeof(br),&br,NULL);
   uint32_t spi_a=r32(0x5C3C), cc_a=r32(0x9C1C), rlc_a=r32(0x3D64);
   printf("  Bank%d grbm=0x%08X: SPI 0x%08X CC 0x%08X RLC 0x%08X\n",b,BANKS[b],spi_a,cc_a,rlc_a);
   if((spi_a & 0x1F)==0x1F) printf("    *** SPI UNLOCKED ON THIS BANK ***\n");
 }
 /* Reset GRBM to all-broadcast (gfx10.1: bits 29,30,31) */
 REG_IO grst={0x34D0,0xE0000000}; DeviceIoControl(g_h,0x80000B8C,&grst,sizeof(grst),&grst,sizeof(br),&br,NULL);
 uint32_t grbm2=r32(0x34D0);
 uint32_t spi_f=r32(0x5C3C), cc_f=r32(0x9C1C), rlc_f=r32(0x3D64);
 printf("After: GRBM=0x%08X SPI=0x%08X CC=0x%08X RLC=0x%08X\n",grbm2,spi_f,cc_f,rlc_f);
 /* QueryActiveWgp (Q0 0x1E) */
 REG_IO qa={0x03B10A08,0x1E}; DeviceIoControl(g_h,0x80000B8C,&qa,sizeof(qa),&qa,sizeof(br),&br,NULL);
 Sleep(20);
 uint32_t arg=smn_r(0x03B10A48);
 printf("Q0 0x1E (QueryActiveWgp) arg=0x%08X (%u WGPs)\n",arg,arg);
 /* Scratch and GRBM_STATUS */
 printf("Scratch=0x%08X GRBM_STATUS=0x%08X\n",r32(0x32D4),r32(0x3260));
 /* Also try via IOCTL_UNLOCK_40CU for comparison */
 printf("\n=== Trying IOCTL 0x80000980 (UNLOCK_40CU with enable=1) ===\n");
 uint32_t in=1; DWORD ret2=0;
 DeviceIoControl(g_h,IOCTL_UNLOCK_40CU,&in,sizeof(in),&in,sizeof(in),&ret2,NULL);
 printf("IOCTL_UNLOCK_40CU ret=0x%X in_after=0x%08X\n",ret2,in);
 printf("Post-IOCTL: SPI=0x%08X CC=0x%08X\n",r32(0x5C3C),r32(0x9C1C));
 return 0;
}

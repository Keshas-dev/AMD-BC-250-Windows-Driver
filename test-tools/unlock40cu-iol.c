/* unlock40cu-iol.c — call IOCTL 0x80000980 (UNLOCK_40CU) and verify */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#define IOCTL_INIT 0x80000B80
#define IOCTL_R    0x80000B88
#define IOCTL_UNLOCK 0x80000980
typedef struct{uint32_t off;uint32_t val;}RIO;
typedef struct{uint64_t base;uint32_t sz;uint32_t flags;uint64_t fb;uint32_t fbsz;}IHW;
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 HANDLE h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(h==INVALID_HANDLE_VALUE){printf("FAIL open %lu\n",GetLastError());return 1;}
 DWORD br;
 IHW ih={0xFE800000ULL,0x80000,1,0,0};
 DeviceIoControl(h,IOCTL_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL);
 /* Baseline */
 RIO r={0x5C3C,0}; DeviceIoControl(h,IOCTL_R,&r,sizeof(r),&r,sizeof(r),&br,NULL);
 uint32_t spi0=r.val;
 r.off=0x9C1C; r.val=0; DeviceIoControl(h,IOCTL_R,&r,sizeof(r),&r,sizeof(r),&br,NULL);
 uint32_t cc0=r.val;
 r.off=0x3D64; r.val=0; DeviceIoControl(h,IOCTL_R,&r,sizeof(r),&r,sizeof(r),&br,NULL);
 uint32_t rlc0=r.val;
 printf("Before: SPI=0x%X CC=0x%X RLC=0x%X\n",spi0,cc0,rlc0);
 /* UNLOCK 40CU */
 uint32_t in=1;
 BOOL ok=DeviceIoControl(h,IOCTL_UNLOCK,&in,sizeof(in),&in,sizeof(in),&br,NULL);
 printf("IOCTL_UNLOCK_40CU ok=%d gle=%lu in_after=0x%X\n",ok,GetLastError(),in);
 /* After */
 r.off=0x5C3C; r.val=0; DeviceIoControl(h,IOCTL_R,&r,sizeof(r),&r,sizeof(r),&br,NULL);
 uint32_t spi1=r.val;
 r.off=0x9C1C; r.val=0; DeviceIoControl(h,IOCTL_R,&r,sizeof(r),&r,sizeof(r),&br,NULL);
 uint32_t cc1=r.val;
 r.off=0x3D64; r.val=0; DeviceIoControl(h,IOCTL_R,&r,sizeof(r),&r,sizeof(r),&br,NULL);
 uint32_t rlc1=r.val;
 printf("After:  SPI=0x%X CC=0x%X RLC=0x%X\n",spi1,cc1,rlc1);
 if((spi1&0x1F)==0x1F) printf("*** SPI UNLOCKED ***\n");
 CloseHandle(h);
 return 0;
}

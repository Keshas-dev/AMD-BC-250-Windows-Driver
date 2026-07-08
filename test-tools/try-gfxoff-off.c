/* try-gfxoff-off.c — minimal: only disable GFXOFF bit, nothing else dangerous */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v;
    return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL);
}
static uint32_t R32(uint32_t o) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0;
    if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static uint32_t smnR(uint32_t a) { W32(0x38,a); R32(0x38); return R32(0x3C); }
static void smnW(uint32_t a, uint32_t v) { W32(0x38,a); W32(0x3C,v); }

static int q0(int msg, uint32_t arg) {
    uint32_t c=smnR(0x03B10A68);
    if(c==1) smnW(0x03B10A68,0); else if(c!=0) smnW(0x03B10A68,0);
    smnW(0x03B10A48,arg); smnW(0x03B10A08,msg);
    for(int i=0;i<1500;i++){
        c=smnR(0x03B10A68);
        if(c==1){Sleep(2);return (int)smnR(0x03B10A48);}
        if(c!=0) return -((int)c); Sleep(1);
    }
    return -100;
}

static int q2(int msg, uint32_t a0, uint32_t a1) {
    smnW(0x03B10564,0); smnW(0x03B10998,a0); smnW(0x03B1099C,a1); smnW(0x03B10528,msg);
    for(int i=0;i<500;i++){
        uint32_t st=smnR(0x03B10564);
        if(st==1) return (int)smnR(0x03B10998);
        if(st==0xFF) return -1;
        Sleep(1);
    }
    smnW(0x03B10564,0); return -100;
}

int main() {
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n");

    int f=q0(0x37,0); int w=q0(0x1E,0); uint32_t x=q0(0x3D,0);
    printf("Before: Freq=%d Wgp=%d Feat=0x%08X GFXOFF=%s\n",f,w,x,(x&4)?"ON":"OFF");

    /* ONLY try Queue 2 test message + disable GFXOFF */
    int t=q2(3,0,0);
    printf("Q2 test=%d (23=OK, -1=FAIL, -100=dead)\n",t);

    if(t!=23){printf("Queue 2 dead, cannot disable GFXOFF\n");CloseHandle(h);return 1;}

    /* Disable GFXOFF — only this one message */
    int r=q2(6,4,0);
    printf("Q2 disable(GFXOFF)=%d\n",r);
    Sleep(200);

    f=q0(0x37,0); w=q0(0x1E,0); x=q0(0x3D,0);
    printf("After:  Freq=%d Wgp=%d Feat=0x%08X GFXOFF=%s\n",f,w,x,(x&4)?"ON":"OFF");

    /* Restore */
    q2(5,4,0);
    printf("Restored GFXOFF\n");

    CloseHandle(h);
    return 0;
}
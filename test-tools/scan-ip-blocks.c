/* scan-ip-blocks.c — Scan BAR5 for living IP blocks. SAFE: reads only proven-alive regions. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");CloseHandle(h);return 1;}

    /* Scan BAR5 at known IP base offsets (safe reads only) */
    printf("BAR5 IP block scan (proven-alive offsets only):\n");
    struct { uint32_t off; const char* desc; } probes[]={
        {0x00000,"GPU_ID/NBIO"},  {0x02000,"GC/GRBM"},   {0x03000,"GC/GRBM2"},
        {0x05000,"DCN HUBPREQ?"}, {0x06000,"DCN OTG?"},   {0x07000,"DCN DMCUB?"},
        {0x08000,"THM"},          {0x09C1C,"CC_ARRAY"},   {0x0DA60,"GFX_RING"},
        {0x12600,"GC_BASE"},      {0x14000,"UMC0"},       {0x16000,"MP0/PSP"},
        {0x16600,"THM(linux)"},   {0x16800,"SMUIO"},      {0x16A00,"SMUIO/SMU"},
        {0x16C00,"CLK"},          {0x17400,"FUSE"},       {0x1A000,"MMHUB"},
    };
    for(int i=0;i<sizeof(probes)/sizeof(probes[0]);i++){
        uint32_t v=R32(probes[i].off);
        printf("  [0x%05X] %-14s = 0x%08X%s\n",probes[i].off,probes[i].desc,v,
            v==0xFFFFFFFF?" (DEAD)":"");
    }

    /* HPD scan - read only HPD2 area that showed activity */
    printf("\nHPD-like registers (0x5400-0x57FF):\n");
    int found=0;
    for(uint32_t off=0x5400;off<0x5800;off+=4){
        uint32_t v=R32(off);
        if(v!=0xFFFFFFFF && v!=0 && v!=0x80840000){
            printf("  [0x%04X] = 0x%08X\n",off,v);
            found++;
        }
    }
    if(!found) printf("  (no non-default HPD values found)\n");

    CloseHandle(h);
    return 0;
}

/* psp-tee-test.c — try PSP TEE svc 0x7C via ring (probe) */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"
#define IOCTL_RING_SUBMIT 0x80000C1C
#define IOCTL_RING_INIT 0x80000C18
static HANDLE h;
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(h==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 DWORD br=0; uint32_t initIn=0; uint32_t initOut[6]={0};
 if(!DeviceIoControl(h,IOCTL_RING_INIT,&initIn,sizeof(initIn),initOut,sizeof(initOut),&br,NULL)){printf("RING_INIT FAIL %u\n",GetLastError());return 1;}
 printf("RING_INIT ok Result=%u C64 0x%08X C81 0x%08X\n",initOut[0],initOut[4],initOut[5]);
 // Try TEE command: use GFX_CMD_ID_LOAD_TA (0x1C) / INVOKE (0x1D) as per psp_gfx_if.h
 // We don't have TA, so expect TEE_ERROR_ITEM_NOT_FOUND or UNKNOWN
 for(int cmd=0x1C; cmd<=0x1E; cmd++){
   uint32_t in[2]={cmd,0}; uint32_t out[6]={0};
   if(DeviceIoControl(h,IOCTL_RING_SUBMIT,in,sizeof(in),out,sizeof(out),&br,NULL)){
     printf("cmd 0x%02X -> Result %u Fence %u Resp 0x%08X\n",cmd,out[0],out[1],out[2]);
   } else printf("cmd 0x%02X DeviceIoControl fail %u\n",cmd,GetLastError());
   Sleep(100);
 }
 // Try direct svc 0x7C as SMN write via PSP? Use custom cmd 0x7C
 {
   uint32_t in[3]={0x7C, 0x0900C234, 0}; uint32_t out[6]={0};
   if(DeviceIoControl(h,IOCTL_RING_SUBMIT,in,sizeof(in),out,sizeof(out),&br,NULL)){
     printf("svc 0x7C probe 0x0900C234 -> Result %u Resp 0x%08X\n",out[0],out[2]);
   }
 }
 CloseHandle(h); return 0;
}

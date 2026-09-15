#include <windows.h>
#include <stdio.h>
int main(){
 HANDLE h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(h==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 typedef struct{ UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize;} INIT_HW;
 INIT_HW ih; DWORD ret=0; ZeroMemory(&ih,sizeof(ih));
 ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=0; // Flags=0 = full init including GART (HwInitGart=1)
 ih.FbPhysicalBase=0; ih.FbSize=0;
 printf("Calling INIT_HW Flags=0 with HwInitGart=1 HwInitMaxStep=9...\n");
 BOOL ok=DeviceIoControl(h,0x80000B80,&ih,sizeof(ih),&ih,sizeof(ih),&ret,NULL);
 printf("Result ok=%d gle=%u ret=%u\n",ok,GetLastError(),ret);
 if(ok) printf("FbPhysicalBase=0x%llX FbSize=0x%X\n",ih.FbPhysicalBase,ih.FbSize);
 CloseHandle(h);
 // Now probe SPI
 HANDLE h2=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 typedef struct{ UINT32 RegisterOffset; UINT32 Value;} REG_IO;
 REG_IO r; DWORD ret2=0;
 r.RegisterOffset=0x5C3C; r.Value=0;
 DeviceIoControl(h2,0x80000B88,&r,sizeof(r),&r,sizeof(r),&ret2,NULL);
 printf("SPI_PG after GART init: 0x%08X\n",r.Value);
 CloseHandle(h2);
 return 0;
}

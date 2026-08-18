/* spi-pg-psp-prog.c - try PSP REG_PROG IOCTL for privileged write */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

#define IOCTL_PSP_INIT_HW      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_READ_REG     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_WRITE_REG    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_REG_PROG     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_GET_STATUS   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct { ULONG64 PhysicalAddress; ULONG Size; } PSP_INIT_HW_REQUEST;
typedef struct { ULONG Offset; } PSP_READ_REQUEST;
typedef struct { ULONG Offset; ULONG Value; } PSP_WRITE_REQUEST;
typedef struct { ULONG Offset; ULONG Value; } PSP_READ_RESPONSE;

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HANDLE hPsp = CreateFileA("\\\\.\\AmdBcPsp",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(hPsp==INVALID_HANDLE_VALUE){printf("FAIL: open PSP err=%lu\n",GetLastError());return 1;}
    printf("PSP driver opened\n");

    PSP_INIT_HW_REQUEST initReq = {0xFE800000ULL, 0x80000};
    DWORD br=0;
    if(!DeviceIoControl(hPsp,IOCTL_PSP_INIT_HW,&initReq,sizeof(initReq),NULL,0,&br,NULL)){
        printf("INIT_HW failed err=%lu\n",GetLastError());
        return 1;
    }
    printf("INIT_HW OK\n");

    /* Read baseline */
    PSP_READ_REQUEST readReq = {0x5C3C};
    PSP_READ_RESPONSE readResp = {0};
    DWORD rb;
    DeviceIoControl(hPsp,IOCTL_PSP_READ_REG,&readReq,sizeof(readReq),&readResp,sizeof(readResp),&rb,NULL);
    printf("Baseline SPI_PG = 0x%08X\n",readResp.Value);

    /* Method 1: Standard write */
    PSP_WRITE_REQUEST writeReq = {0x5C3C, 0x1F};
    DeviceIoControl(hPsp,IOCTL_PSP_WRITE_REG,&writeReq,sizeof(writeReq),NULL,0,&br,NULL);
    readReq.Offset = 0x5C3C;
    DeviceIoControl(hPsp,IOCTL_PSP_READ_REG,&readReq,sizeof(readReq),&readResp,sizeof(readResp),&rb,NULL);
    printf("After WRITE_REG: SPI_PG = 0x%08X\n",readResp.Value);

    /* Method 2: REG_PROG IOCTL (privileged) */
    ULONG regProgData[2] = {0x5C3C, 0x1F};
    if(DeviceIoControl(hPsp,IOCTL_PSP_REG_PROG,regProgData,sizeof(regProgData),NULL,0,&br,NULL)){
        printf("REG_PROG write OK\n");
        readReq.Offset = 0x5C3C;
        DeviceIoControl(hPsp,IOCTL_PSP_READ_REG,&readReq,sizeof(readReq),&readResp,sizeof(readResp),&rb,NULL);
        printf("After REG_PROG: SPI_PG = 0x%08X %s\n",readResp.Value,(readResp.Value==0x1F)?"*** SUCCESS! ***":"");
    } else {
        printf("REG_PROG failed err=%lu\n",GetLastError());
    }

    /* Method 3: Try different SPI_PG addresses (maybe BC-250 uses different offset) */
    printf("\n=== Trying alternate SPI_PG offsets ===\n");
    uint32_t altOffsets[] = {0x34FC, 0x3460, 0x3264, 0x5C38, 0x5C40};
    for(int i=0; i<5; i++){
        writeReq.Offset = altOffsets[i];
        writeReq.Value = 0x1F;
        DeviceIoControl(hPsp,IOCTL_PSP_WRITE_REG,&writeReq,sizeof(writeReq),NULL,0,&br,NULL);
        readReq.Offset = altOffsets[i];
        DeviceIoControl(hPsp,IOCTL_PSP_READ_REG,&readReq,sizeof(readReq),&readResp,sizeof(readResp),&rb,NULL);
        printf("  Offset 0x%04X: wrote 0x1F read 0x%08X %s\n",altOffsets[i],readResp.Value,(readResp.Value==0x1F)?"STUCK!":"");
    }

    CloseHandle(hPsp);
    printf("DONE\n");
    return 0;
}

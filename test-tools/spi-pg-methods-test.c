/* spi-pg-methods-test.c - try every method to write SPI_PG */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

/* Kernel functions for interrupt control */
NTKERNELAPI VOID KeRaiseIrql(KIRQL NewIrql, PKIRQL OldIrql);
NTKERNELAPI VOID KeLowerIrql(KIRQL OldIrql);

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static void smnW(uint32_t a,uint32_t v){W32(0x38,a);W32(0x3C,v);}
static uint32_t smnR(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL\n");return 1;}

    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br=0;
    memset(&ih,0,sizeof(ih));
    ih.MmioPhysicalBase=0xFE800000ULL;
    ih.MmioSize=0x80000;
    ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n");

    printf("Baseline SPI_PG=0x%08X\n", R32(0x5C3C));

    /* Method 1: Write via WRITE_REGISTER_ULONG (standard) */
    printf("\n=== Method 1: Standard WRITE_REGISTER ===\n");
    W32(0x5C3C, 0x1F);
    printf("  After: 0x%08X\n", R32(0x5C3C));

    /* Method 2: Write with different values */
    printf("\n=== Method 2: Different values ===\n");
    uint32_t vals[] = {0x01,0x02,0x04,0x08,0x10,0x1F,0xFF,0x1FF,0xFFFF,0xFFFFFFFF};
    for(int i=0;i<10;i++){
        W32(0x5C3C, vals[i]);
        uint32_t rb = R32(0x5C3C);
        printf("  Wrote 0x%08X -> Read 0x%08X %s\n", vals[i], rb, (rb==vals[i])?"STUCK!":"");
    }

    /* Method 3: Write with interrupts off (KeRaiseIrql) */
    printf("\n=== Method 3: With interrupts disabled ===\n");
    KIRQL oldIrql;
    KeRaiseIrql(HIGH_LEVEL, &oldIrql);
    W32(0x5C3C, 0x1F);
    uint32_t rb3 = R32(0x5C3C);
    KeLowerIrql(oldIrql);
    printf("  After: 0x%08X\n", rb3);

    /* Method 4: Multiple writes (some regs need 2 writes) */
    printf("\n=== Method 4: Double write ===\n");
    W32(0x5C3C, 0x1F);
    W32(0x5C3C, 0x1F);
    printf("  After double: 0x%08X\n", R32(0x5C3C));

    /* Method 5: Write via SMN (NBIO 0x38/0x3C) - SPI_PG is in GC, not SMN, but try */
    printf("\n=== Method 5: Via SMN (0x38/0x3C) - SPI_PG physical addr ===\n");
    /* SPI_PG is at BAR5 0x5C3C = physical 0xFE805C3C. Try SMN with this addr */
    smnW(0xFE805C3C, 0x1F);
    printf("  After SMN write: 0x%08X\n", R32(0x5C3C));

    printf("\n=== DONE ===\n");
    CloseHandle(h);
    return 0;
}

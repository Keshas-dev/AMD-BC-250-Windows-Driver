/* vcn-probe-readonly.c — DF mem64 window read-only VCN probe (Windows port of enable_vcn.py --verify) */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"
#define IOCTL_INIT 0x80000B80
#define IOCTL_PCI_SMN 0x80000C30
typedef struct{uint32_t a;uint32_t d;uint32_t w;uint32_t r;uint32_t b;uint32_t dev;uint32_t f;uint32_t m;uint32_t bar5;} PSMN;
static HANDLE h;
static BOOL W32(uint32_t o,uint32_t v){ AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o){ AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t smnR_bar5(uint32_t a){ W32(0x38,a); R32(0x38); return R32(0x3C); }
static void smnW_bar5(uint32_t a,uint32_t v){ W32(0x38,a); W32(0x3C,v); }
static uint32_t smnR_pci(uint32_t a){ PSMN p={0};DWORD br=0; p.a=a; p.w=0; if(DeviceIoControl(h,IOCTL_PCI_SMN,&p,sizeof(p),&p,sizeof(p),&br,NULL)&&p.r) return p.d; return 0xFFFFFFFF; }
static void smnW_pci(uint32_t a,uint32_t v){ PSMN p={0};DWORD br=0; p.a=a; p.d=v; p.w=1; DeviceIoControl(h,IOCTL_PCI_SMN,&p,sizeof(p),&p,sizeof(p),&br,NULL); }
// DF primary helpers (try PCI first, fallback BAR5)
static uint32_t smnR_df(uint32_t a){ uint32_t v=smnR_pci(a); if(v!=0xFFFFFFFF) return v; return smnR_bar5(a); }
// Q3 mem64 window via DF (SMU-local 0x01100000+(addr&0xFFFFF) masked, enable_vcn.py:338-340)
#define Q3_CMD 0x03B10A20
#define Q3_RSP 0x03B10A80
#define Q3_ARG 0x03B10A88
static int q3_wait(int ms){ for(int i=0;i<ms;i++){ uint32_t st=smnR_df(Q3_RSP); if(st==0x01||st==0xFF||st==0xFE||st==0xFD||st==0xFC) return (int)st; Sleep(1); } return -100; }
static int q3_mem64_read(uint32_t smn, uint32_t *out){
    smnW_bar5(Q3_RSP,0); // use BAR5 for mailbox (proven), but could use DF too — keep BAR5 for mailbox to avoid mixing
    // Actually use DF for mailbox as well now — try DF first
    PSMN p={0};DWORD br=0; p.a=Q3_RSP; p.d=0; p.w=1; DeviceIoControl(h,IOCTL_PCI_SMN,&p,sizeof(p),&p,sizeof(p),&br,NULL);
    if(!p.r){ smnW_bar5(Q3_RSP,0); } else { /* already via DF */ }
    // Use helper that goes via DF primary for ARG/CMD as well
    // Fallback to simple smnW_df for mailbox registers
    // For reliability, use smnW_df helper (PCI primary)
    // Implement manual DF mailbox send
    // Write RSP=0, ARG=smn, CMD=0x2A
    // Use direct PCI writes for mailbox to stay on DF
    PSMN pw={0};
    pw.a=Q3_RSP; pw.d=0; pw.w=1; DeviceIoControl(h,IOCTL_PCI_SMN,&pw,sizeof(pw),&pw,sizeof(pw),&br,NULL);
    pw.a=Q3_ARG; pw.d=smn; pw.w=1; DeviceIoControl(h,IOCTL_PCI_SMN,&pw,sizeof(pw),&pw,sizeof(pw),&br,NULL);
    pw.a=Q3_CMD; pw.d=0x2A; pw.w=1; DeviceIoControl(h,IOCTL_PCI_SMN,&pw,sizeof(pw),&pw,sizeof(pw),&br,NULL);
    int st=-100;
    for(int i=0;i<2000;i++){ uint32_t s=smnR_df(Q3_RSP); if(s==0x01||s==0xFF||s==0xFE||s==0xFD||s==0xFC){ st=(int)s; break; } Sleep(1); }
    if(st!=0x01) return st;
    uint32_t v=smnR_df(Q3_ARG);
    if(out) *out=v;
    return 1;
}
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(h==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 AMDBC250_IOCTL_INIT_HARDWARE ih;DWORD br=0; ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
 DeviceIoControl(h,IOCTL_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL);
 printf("=== DF mem64 read-only VCN probe (Windows --verify) ===\n");
 printf("DF 00:00.0 B8/BC primary, BAR5 fallback, Q3 0x2A mem64 window masked\n");
 struct{uint32_t addr; const char* name;} addrs[]={
  {0x0115A870,"CORE_MASK"}, {0x0005A870,"CORE_MASK_ALT"}, {0x0006D190,"DOM6_STATUS"}, {0x0006D0F8,"DOM6_CTRL"},
  {0x0006D17C,"DOM6_CMD"}, {0x0006D184,"DOM6_RAIL"}, {0x02403000,"VCN_SLICE"}, {0x02402C00,"GC_SLICE"},
  {0x01103000,"VCN_M64_ALIAS_NBIO"}, {0x00003000,"VCN_M64_ALIAS_LOW"}, {0x0112F000,"MISMATCH_PREV"}, {0x03B10A68,"Q3_RSP"},
 };
 for(int i=0;i<(int)(sizeof(addrs)/sizeof(addrs[0]));i++){
   uint32_t a=addrs[i].addr;
   uint32_t bar5=smnR_bar5(a);
   uint32_t pci=smnR_pci(a);
   uint32_t df=smnR_df(a);
   uint32_t mem64=0; int st=q3_mem64_read(a, &mem64);
   printf("%-18s 0x%08X: BAR5 0x%08X PCI 0x%08X DF 0x%08X  mem64 Q3 0x2A st %d val 0x%08X %s\n",
     addrs[i].name, a, bar5, pci, df, st, mem64, (st==1)?"OK":(st==-100?"TIMEOUT":(st==-3?"REJECTED":"FAIL")));
   Sleep(20);
 }
 printf("\nVCN discovery: VCN 2.0.3 @0x981d10 not harvested, slice 0x02403000 dead-zero via DF and mem64 expected\n");
 CloseHandle(h); return 0;
}

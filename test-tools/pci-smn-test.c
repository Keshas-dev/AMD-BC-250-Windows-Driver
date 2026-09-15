/* pci-smn-test.c — retest SMN via PCI config 00:00.0 0xB8/0xBC (Linux Bc250PciTransport) */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#define IOCTL_INIT 0x80000B80
#define IOCTL_PCI_SMN 0x80000C30
#define FILE_DEVICE_AMDBC250 0x8000
typedef struct{ uint64_t base; uint32_t sz; uint32_t flags; uint64_t fb; uint32_t fbsz; } INIT_HW;
typedef struct{ uint32_t SmnAddress; uint32_t SmnData; uint32_t IsWrite; uint32_t Result; uint32_t Bus; uint32_t Device; uint32_t Function; uint32_t Method; uint32_t Bar5SmnData; } PCI_SMN;
static HANDLE g_h;
static BOOL pci_read(uint32_t smn, uint32_t *out, uint32_t *bar5out, uint32_t *method){
    PCI_SMN p={0}; DWORD br=0;
    p.SmnAddress=smn; p.IsWrite=0; p.Bus=0; p.Device=0; p.Function=0;
    if(!DeviceIoControl(g_h, IOCTL_PCI_SMN, &p, sizeof(p), &p, sizeof(p), &br, NULL)) return FALSE;
    if(out) *out=p.SmnData;
    if(bar5out) *bar5out=p.Bar5SmnData;
    if(method) *method=p.Method;
    return p.Result==1;
}
static BOOL pci_write(uint32_t smn, uint32_t val){
    PCI_SMN p={0}; DWORD br=0;
    p.SmnAddress=smn; p.SmnData=val; p.IsWrite=1; p.Bus=0; p.Device=0; p.Function=0;
    if(!DeviceIoControl(g_h, IOCTL_PCI_SMN, &p, sizeof(p), &p, sizeof(p), &br, NULL)) return FALSE;
    return p.Result==1;
}
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 g_h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(g_h==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 INIT_HW ih={0xFE800000ULL,0x80000,1,0,0}; DWORD br=0;
 DeviceIoControl(g_h, IOCTL_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);
 printf("=== PCI config SMN retest (00:00.0 B8/BC) ===\n");
 printf("Build: atikmdag.sys with IOCTL 0x80000C38 (CF8/CFC)\n");
 struct{ uint32_t addr; const char* name; } tests[]={
  {0x03B10A08,"Q0_CMD"}, {0x03B10A68,"Q0_RSP"}, {0x03B10A48,"Q0_ARG"},
  {0x03B10528,"Q2_CMD"}, {0x03B10564,"Q2_RSP"}, {0x03B10998,"Q2_ARG"},
  {0x03B10A20,"Q3_CMD"}, {0x03B10A80,"Q3_RSP"}, {0x03B10A88,"Q3_ARG"},
  {0x0115A870,"CORE_MASK"}, {0x03B10000,"THM0"}, {0x0006D190,"DOM6_STATUS"},
  {0x0006D0F8,"DOM6_CTRL"}, {0x02403000,"VCN_SLICE"}, {0x02402C00,"GC_SLICE"},
 };
 for(int i=0;i<(int)(sizeof(tests)/sizeof(tests[0]));i++){
   uint32_t v=0, bar5=0, meth=0;
   BOOL ok=pci_read(tests[i].addr, &v, &bar5, &meth);
   printf("%-12s SMN 0x%08X: PCI=0x%08X BAR5=0x%08X meth=%u %s\n",
     tests[i].name, tests[i].addr, v, bar5, meth, ok?"OK":"FAIL");
   Sleep(5);
 }
 printf("\n--- Write test (CORE_MASK 0x5A870) ---\n");
 uint32_t before=0, bar5b=0; pci_read(0x0115A870,&before,&bar5b,NULL);
 printf("Before CORE_MASK PCI=0x%08X BAR5=0x%08X\n",before,bar5b);
 /* Try write same value back */
 BOOL wok=pci_write(0x0115A870, before);
 uint32_t after=0, bar5a=0; pci_read(0x0115A870,&after,&bar5a,NULL);
 printf("Write 0x%08X via PCI B8/BC: %s -> after PCI=0x%08X BAR5=0x%08X %s\n",
   before, wok?"OK":"FAIL", after, bar5a, (after==before)?"STUCK/SAME":"CHANGED");
 printf("\n--- ECAM probe via existing 0x80000BAC ---\n");
 /* ECAM read of B0:D0:F0 offset 0x00 */
 #define IOCTL_READ_PCI_CONFIG 0x80000BAC
 typedef struct{ uint32_t Bus,Device,Function,BytesRead; uint8_t ConfigData[256]; } PCI_CFG;
 PCI_CFG pc={0}; pc.Bus=0; pc.Device=0; pc.Function=0;
 DWORD br2=0;
 if(DeviceIoControl(g_h, IOCTL_READ_PCI_CONFIG, &pc, sizeof(pc), &pc, sizeof(pc), &br2, NULL)){
   uint16_t ven=pc.ConfigData[0]|(pc.ConfigData[1]<<8);
   uint16_t dev=pc.ConfigData[2]|(pc.ConfigData[3]<<8);
   printf("ECAM/CF8 B0:D0:F0 VEN=0x%04X DEV=0x%04X BytesRead=%u\n",ven,dev,pc.BytesRead);
   printf("B8/BC bytes: B8=%02X %02X %02X %02X BC=%02X %02X %02X %02X\n",
     pc.ConfigData[0xB8],pc.ConfigData[0xB9],pc.ConfigData[0xBA],pc.ConfigData[0xBB],
     pc.ConfigData[0xBC],pc.ConfigData[0xBD],pc.ConfigData[0xBE],pc.ConfigData[0xBF]);
 } else printf("READ_PCI_CONFIG failed %u\n",GetLastError());
 CloseHandle(g_h);
 return 0;
}

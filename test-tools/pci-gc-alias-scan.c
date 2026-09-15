/* pci-gc-alias-scan.c — SMN GC alias scan via DF PCI config 00:00.0 B8/BC (new IOCTL 0x80000C38) + BAR5 compare */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "..\inc\amdbc250_ioctl.h"
#define MASK_REG 0x0115A870UL
#define MSG_WRITE_FF 0x98UL
#define Q3_CMD 0x03B10A20UL
#define Q3_RSP 0x03B10A80UL
#define Q3_ARG 0x03B10A88UL
#define GRBM_GFX_INDEX 0x34D0
#define SPI_PG 0x5C3C
#define RLC_PG 0x3D64
#define CC_ARRAY 0x9C1C
#define GPU_ID 0x0000
#define GRBM_STATUS 0x3260
#define SCRATCH 0x32D4
static HANDLE h;
static FILE *lf=NULL;
static void Trace(const char *fmt, ...){ va_list ap,ap2; va_start(ap,fmt); if(lf){va_copy(ap2,ap); vfprintf(lf,fmt,ap2); va_end(ap2); fflush(lf);} vprintf(fmt,ap); fflush(stdout); va_end(ap); }
static BOOL W32(uint32_t o,uint32_t v){ AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, &r,sizeof(r), &r,sizeof(r), &b,NULL); }
static uint32_t R32(uint32_t o){ AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &r,sizeof(r), &r,sizeof(r), &b,NULL)) return r.Value; return 0xFFFFFFFF; }
static void smnW_bar5(uint32_t a,uint32_t v){ W32(0x38,a); W32(0x3C,v); }
static uint32_t smnR_bar5(uint32_t a){ W32(0x38,a); R32(0x38); return R32(0x3C); }
static uint32_t smnR_pci(uint32_t a){ AMDBC250_IOCTL_PCI_SMN_ACCESS p={0}; DWORD br=0; p.SmnAddress=a; p.IsWrite=0; p.Bus=0; p.Device=0; p.Function=0; BOOL ok=DeviceIoControl(h, IOCTL_AMDBC250_PCI_SMN_ACCESS, &p,sizeof(p), &p,sizeof(p), &br,NULL); if(!ok){ Trace("  PCI IOCTL failed addr 0x%08X gle=%lu br=%u\n",a,GetLastError(),br); return 0xFFFFFFFF; } if(!p.Result){ Trace("  PCI Result 0 addr 0x%08X meth %u data 0x%08X\n",a,p.Method,p.SmnData); return 0xFFFFFFFF; } return p.SmnData; }
static uint32_t smnR_both(uint32_t a, uint32_t *pciOut){ uint32_t pci=smnR_pci(a); Sleep(2); uint32_t bar5=smnR_bar5(a); Sleep(2); if(pciOut) *pciOut=pci; if(bar5!=pci) Trace("  MISMATCH SMN 0x%08X BAR5=0x%08X PCI=0x%08X\n",a,bar5,pci); return bar5; }
static int smu_send(uint32_t msg,uint32_t arg){
    // Use BAR5 SMN for mailbox (proven); could use PCI but keep BAR5 for now
    for(int i=0;i<2500;i++){ uint32_t st=smnR_bar5(Q3_RSP); if(st==0x01||st==0xFF||st==0xFE||st==0xFD||st==0xFC) break; Sleep(2); }
    smnW_bar5(Q3_RSP,0); smnW_bar5(Q3_ARG,arg); smnW_bar5(Q3_ARG+4,0); smnW_bar5(Q3_CMD,msg);
    for(int i=0;i<2500;i++){ uint32_t st=smnR_bar5(Q3_RSP); if(st==0x01) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2; if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(2); } return -100;
}
static void grbm_select(uint32_t v){ W32(GRBM_GFX_INDEX,v); }
static const uint32_t BANK_SELECT[]={0x00000000,0x00000100,0x00010000,0x00010100,0x15000000};
static const char *BANK_NAME[]={"SE0/SH0","SE0/SH1","SE1/SH0","SE1/SH1","BCAST"};
static const struct{uint32_t lo,hi;} SAFE_RANGES[]={{0x01100000UL,0x01200000UL},{0x03B10000UL,0x03B11000UL}};
static const struct{uint32_t lo,hi;} EXT_RANGES[]={{0x02400000UL,0x02404000UL},{0x0006D000UL,0x0006D200UL}};
int main(int argc,char **argv){
 BOOL doScan=FALSE,doExt=FALSE,doWrite=FALSE; uint32_t writeAddr=0;
 for(int i=1;i<argc;i++){
  if(!_stricmp(argv[i],"-scan")) doScan=TRUE;
  else if(!_stricmp(argv[i],"-ext")) doExt=TRUE;
  else if(!_stricmp(argv[i],"-write") && i+1<argc){ char *end=NULL; uint32_t v=(uint32_t)strtoul(argv[i+1],&end,16); if(end==argv[i+1]||*end!='\0'){printf("bad -write\n");return 2;} doWrite=TRUE; writeAddr=v; i++; }
  else Trace("unknown arg: %s\n",argv[i]);
 }
 char logPath[MAX_PATH]; GetModuleFileNameA(NULL,logPath,MAX_PATH);{char *dot=strrchr(logPath,'.'); if(dot)*dot='\0'; strncat(logPath,".log",MAX_PATH-strlen(logPath)-1);} lf=fopen(logPath,"w"); setvbuf(stdout,NULL,_IONBF,0);
 Trace("=== PCI GC alias scan (DF B8/BC vs BAR5 38/3C) ===\n"); Trace("log:%s doScan=%d doExt=%d doWrite=%d addr=0x%08X\n",logPath,doScan,doExt,doWrite,writeAddr);
 h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(h==INVALID_HANDLE_VALUE){Trace("FAIL CreateFile gle=%lu\n",GetLastError()); if(lf)fclose(lf); return 1;}
 AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0; ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
 if(!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih,sizeof(ih), &ih,sizeof(ih), &br,NULL)){Trace("INIT FAILED %lu\n",GetLastError()); CloseHandle(h); if(lf)fclose(lf); return 1;}
 Trace("INIT OK\n");
 Trace("\n=== Step1 per-bank GRBM ===\n"); uint32_t gpuId=R32(GPU_ID); uint32_t grbmStatus=R32(GRBM_STATUS); uint32_t grbmIdxLive=R32(GRBM_GFX_INDEX);
 Trace("GPU_ID=0x%08X GRBM_STATUS=0x%08X GRBM_IDX=0x%08X\n",gpuId,grbmStatus,grbmIdxLive);
 Trace("Bank SPI_PG RLC_PG CC_ARRAY GFX_IDX\n");
 for(int i=0;i<5;i++){ grbm_select(BANK_SELECT[i]); uint32_t idxAfter=R32(GRBM_GFX_INDEX); uint32_t spi=R32(SPI_PG); uint32_t rlc=R32(RLC_PG); uint32_t cc=R32(CC_ARRAY); Trace("%-10s 0x%08X 0x%08X 0x%08X 0x%08X\n",BANK_NAME[i],spi,rlc,cc,idxAfter); }
 grbm_select(0x15000000);
 Trace("\n=== Step1b per-bank WRITE ===\n");
 for(int i=0;i<5;i++){ grbm_select(BANK_SELECT[i]); W32(SPI_PG,0x1F); W32(RLC_PG,0x1F); W32(CC_ARRAY,0xFFE00000UL); uint32_t spi=R32(SPI_PG); uint32_t rlc=R32(RLC_PG); uint32_t cc=R32(CC_ARRAY); Trace("%-10s 0x%08X 0x%08X 0x%08X\n",BANK_NAME[i],spi,rlc,cc); }
 grbm_select(0x15000000); Trace("GRBM_STATUS=0x%08X SCRATCH=0x%08X\n",R32(GRBM_STATUS),R32(SCRATCH));
 // Step: DF vs BAR5 compare on key regs
 Trace("\n=== Step DF vs BAR5 compare ===\n");
 uint32_t addrs[]={0x03B10A08,0x03B10A68,0x03B10A48,0x0115A870,0x0006D190,0x0006D0F8,0x02402C00,0x02403000,0x03B10000};
 for(int i=0;i<(int)(sizeof(addrs)/sizeof(addrs[0]));i++){ uint32_t pci=0; uint32_t bar5=smnR_both(addrs[i],&pci); Trace("SMN 0x%08X BAR5=0x%08X PCI=0x%08X %s\n",addrs[i],bar5,pci,(bar5==pci)?"==":"DIFF"); }
 if(doScan){
  Trace("\n=== Step2 SAFE scan (BAR5+PCI) ===\n"); int fg=0,fc=0;
  for(int r=0;r<(int)(sizeof(SAFE_RANGES)/sizeof(SAFE_RANGES[0]));r++){ uint32_t lo=SAFE_RANGES[r].lo, hi=SAFE_RANGES[r].hi; Trace("range 0x%08X-0x%08X\n",lo,hi); uint64_t iter=0; for(uint64_t a=lo;a<hi;a+=0x100){ if((iter&0xFF)==0) Trace(" at 0x%08X fg=%d fc=%d\n",(uint32_t)a,fg,fc); iter++; uint32_t pci=0; uint32_t v=smnR_both((uint32_t)a,&pci); if(v==gpuId){Trace(" GPU_ID SMN 0x%08X BAR5 0x%08X PCI 0x%08X\n",(uint32_t)a,v,pci); fg++;} if(v==0xFFF80000UL||v==0xFFE00000UL){Trace(" CC SMN 0x%08X 0x%08X PCI 0x%08X\n",(uint32_t)a,v,pci); fc++;} if(v==0x07 && (a&0xFF)==0x3C) Trace(" SPI 0x07 cand SMN 0x%08X\n",(uint32_t)a); } }
  Trace("SAFE done fg=%d fc=%d\n",fg,fc);
 }
 if(doExt){
  Trace("\n=== Step2b EXT scan 0x024 + 0x0006D via PCI+BAR5 ===\n");
  for(int r=0;r<(int)(sizeof(EXT_RANGES)/sizeof(EXT_RANGES[0]));r++){ uint32_t lo=EXT_RANGES[r].lo, hi=EXT_RANGES[r].hi; Trace("ext range 0x%08X-0x%08X step 0x4\n",lo,hi); for(uint64_t a=lo;a<hi;a+=4){ uint32_t pci=0; uint32_t v=smnR_both((uint32_t)a,&pci); if(v!=0 && v!=0xFFFFFFFF) Trace(" EXT SMN 0x%08X BAR5 0x%08X PCI 0x%08X\n",(uint32_t)a,v,pci); } }
 }
 if(doWrite){
  if(writeAddr==0 || (writeAddr>=0x03B10000UL && writeAddr<=0x03B11000UL)){Trace("REFUSING unsafe 0x%08X\n",writeAddr); CloseHandle(h); if(lf)fclose(lf); return 2;}
  Trace("\n=== Step3 SMU 0x98 write SMN 0x%08X ===\n",writeAddr); uint32_t before=smnR_bar5(writeAddr); uint32_t before_pci=smnR_pci(writeAddr); Trace("before BAR5 0x%08X PCI 0x%08X\n",before,before_pci); if(before==0xFFFFFFFF){Trace("dead addr\n"); CloseHandle(h); if(lf)fclose(lf); return 2;} int rr=smu_send(MSG_WRITE_FF,writeAddr); Trace("status %d\n",rr); if(rr!=1){Trace("FAIL %d\n",rr); CloseHandle(h); if(lf)fclose(lf); return 1;} Sleep(200); uint32_t after=smnR_bar5(writeAddr); uint32_t after_pci=smnR_pci(writeAddr); Trace("after BAR5 0x%08X PCI 0x%08X\n",after,after_pci); if(after==0x00FF) Trace("*** STUCK 0x00FF alias! ***\n");
 }
 CloseHandle(h); Trace("\nDONE\n"); if(lf)fclose(lf); return 0;
}

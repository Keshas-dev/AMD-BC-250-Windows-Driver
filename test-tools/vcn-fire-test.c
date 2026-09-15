/* vcn-fire-test.c — DF fire Q3 0x61 (SmuTimeout expected) + engage */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"
#define IOCTL_INIT 0x80000B80
#define IOCTL_PCI_SMN 0x80000C30
typedef struct{uint32_t a;uint32_t d;uint32_t w;uint32_t r;uint32_t b;uint32_t dev;uint32_t f;uint32_t m;uint32_t bar5;} PSMN;
static HANDLE h;
static BOOL W32b(uint32_t o,uint32_t v){ AMDBC250_IOCTL_REG_ACCESS r; DWORD br; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&br,NULL); }
static uint32_t R32b(uint32_t o){ AMDBC250_IOCTL_REG_ACCESS r; DWORD br; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&br,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t smnR_pci(uint32_t a){ PSMN p={0};DWORD br; p.a=a; p.w=0; if(DeviceIoControl(h,IOCTL_PCI_SMN,&p,sizeof(p),&p,sizeof(p),&br,NULL)&&p.r) return p.d; return 0xFFFFFFFF; }
static uint32_t smnR_df(uint32_t a){ uint32_t v=smnR_pci(a); if(v!=0xFFFFFFFF) return v; W32b(0x38,a); R32b(0x38); return R32b(0x3C); }
static void smnW_df(uint32_t a,uint32_t v){ PSMN p={0};DWORD br; p.a=a; p.d=v; p.w=1; if(DeviceIoControl(h,IOCTL_PCI_SMN,&p,sizeof(p),&p,sizeof(p),&br,NULL)&&p.r) return; W32b(0x38,a); W32b(0x3C,v); }
#define Q3_CMD 0x03B10A20
#define Q3_RSP 0x03B10A80
#define Q3_ARG 0x03B10A88
static int q3_send(uint32_t msg,uint32_t arg0){ smnW_df(Q3_RSP,0); smnW_df(Q3_ARG,arg0); smnW_df(Q3_ARG+4,0); smnW_df(Q3_CMD,msg); for(int i=0;i<6000;i++){ uint32_t st=smnR_df(Q3_RSP); if(st==0x01) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2; if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(1);} return -100; }
static int sec_set_write_ptr(uint32_t a){ return q3_send(0x28,a); }
static int sec_write_through(uint32_t v){ return q3_send(0x29,v); }
static int smu_write32(uint32_t a,uint32_t v){ int s=sec_set_write_ptr(a); if(s!=1) return s; return sec_write_through(v); }
#define THUNK_ADDR 0x0003ff00
#define FUN_SLOT_CLK 0x00023744
static uint32_t enc_entry(uint32_t as_,uint32_t fb){ return (0x36|(as_<<8)|((fb/8)<<12)); }
static uint32_t enc_movi_n(uint32_t d,uint32_t imm){ uint32_t v=imm&0x7F; return ((v&0xF)<<12)|(d<<8)|(((v>>4)&7)<<4)|0xC; }
static uint32_t enc_call8(uint32_t pc,uint32_t tgt){ int32_t off=(int32_t)(tgt-((pc+4)&~3))>>2; return (((off&0x3FFFF)<<6)|(2<<4)|5); }
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(h==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 AMDBC250_IOCTL_INIT_HARDWARE ih;DWORD br; ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP; DeviceIoControl(h,IOCTL_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL);
 printf("=== VCN fire via DF Q3 0x61 (expect SmuTimeout ~5s) ===\n");
 // build seq
 uint8_t seq[20]={0}; seq[0]=0x36; seq[1]=0x41; seq[2]=0x00; int off=3; uint32_t slots[3]={0x16,0x17,0x18};
 for(int s=0;s<3;s++){ uint32_t pc=THUNK_ADDR+off; uint32_t movi=enc_movi_n(10,slots[s]); seq[off++]=movi&0xFF; seq[off++]=movi>>8&0xFF; uint32_t call=enc_call8(pc,FUN_SLOT_CLK); seq[off++]=call&0xFF; seq[off++]=call>>8&0xFF; seq[off++]=call>>16&0xFF; } seq[off++]=0x1d; seq[off++]=0xf0;
 printf("seq: "); for(int i=0;i<20;i++) printf("%02X ",seq[i]); printf("\n");
 // install
 for(int i=0;i<20;i+=4){ uint32_t d=seq[i]|(seq[i+1]<<8)|(seq[i+2]<<16)|(seq[i+3]<<24); int s=smu_write32(THUNK_ADDR+i,d); printf(" write 0x%05X 0x%08X st %d %s\n",THUNK_ADDR+i,d,s,s==1?"OK":"FAIL"); if(s!=1) return 1; Sleep(10); }
 int sr=smu_write32(0x776C, THUNK_ADDR); printf("repoint 0x776C->0x3FF00 st %d\n",sr);
 // fire
 printf("fire Q3 0x61 (DF) — expect TIMEOUT ~5s as dispatch wait...\n");
 smnW_df(Q3_RSP,0); smnW_df(Q3_ARG,0); smnW_df(Q3_ARG+4,0); smnW_df(Q3_CMD,0x61);
 int st=-100; for(int i=0;i<7000;i++){ uint32_t s=smnR_df(Q3_RSP); if(s==0x01){st=1;break;} if(s==0xFF||s==0xFE||s==0xFD||s==0xFC){st=s;break;} Sleep(1); }
 if(st==-100) printf(" -> SmuTimeout (expected, dispatch wait)\n"); else printf(" -> st 0x%X %s\n",st,st==1?"OK (unexpected)":"FAIL");
 Sleep(200);
 // probe DOM6
 printf("DOM6 after fire:\n");
 uint32_t addrs[]={0x0006D190,0x0006D0F8,0x0006D17C,0x0006D184};
 const char* names[]={"STATUS","CTRL","CMD","RAIL"};
 for(int i=0;i<4;i++){ uint32_t v=smnR_df(addrs[i]); printf(" %s 0x%06X = 0x%08X\n",names[i],addrs[i],v); }
 // engage per-slot ENABLE +0x08=1 via smu_write32 WIN 0x01100000+(g&FFFFF) — need gate addrs from slot table? Use direct DOM6 gate addrs from vcn-probe? For brevity, just try via WIN as enable_vcn engage does via smu_write32 to WIN+off
 // Instead do simple: try to set DOM6 per-slot enables via WIN (we don't have slot gate addrs, skip engage, just probe VCN slice)
 printf("VCN slice probe via DF:\n");
 uint32_t vcn=smnR_df(0x02403000); uint32_t gc=smnR_df(0x02402C00);
 printf(" VCN 0x02403000 = 0x%08X %s\n",vcn, vcn==0?"dead-zero (fabric closed)":(vcn==0xFFFFFFFF?"dead":"non-zero! fabric open?"));
 printf(" GC  0x02402C00 = 0x%08X\n",gc);
 // disarm
 printf("Disarm 0x776C=0 + wipe\n");
 smu_write32(0x776C,0);
 for(int i=0;i<20;i+=4) smu_write32(THUNK_ADDR+i,0);
 printf("disarm done, check SMU alive: Q3 0x2A 0x0115A870\n");
 smnW_df(Q3_RSP,0); smnW_df(Q3_ARG,0x0115A870); smnW_df(Q3_CMD,0x2A);
 int st2=-100; for(int i=0;i<2000;i++){ uint32_t s=smnR_df(Q3_RSP); if(s==0x01||s==0xFD){st2=s;break;} Sleep(1);} uint32_t val=smnR_df(Q3_ARG);
 printf(" Q3 0x2A st 0x%X val 0x%08X %s\n",st2,val,(st2==1 && val==0xFF)?"OPEN (8c)":(st2==-100?"TIMEOUT (wedge?)":"?"));
 if(st2==-100) printf("SMU wedged — cold cycle needed!\n"); else printf("SMU alive\n");
 CloseHandle(h); return 0;
}

/* vcn-install-nofire.c — DF install --no-fire (smu_write32 0x3FF00 + 0x776C via Q3 0x28/0x29) */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
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
static int q3_send(uint32_t msg, uint32_t arg0){
    smnW_df(Q3_RSP,0); smnW_df(Q3_ARG,arg0); smnW_df(Q3_ARG+4,0); smnW_df(Q3_CMD,msg);
    for(int i=0;i<2000;i++){ uint32_t st=smnR_df(Q3_RSP); if(st==0x01) return 1; if(st==0xFF||st==0xFE||st==0xFD||st==0xFC) return (int)st; Sleep(1); } return -100;
}
static int sec_set_write_ptr(uint32_t addr){ int st=q3_send(0x28,addr); return st; }
static int sec_write_through(uint32_t val){ int st=q3_send(0x29,val); return st; }
static int smu_write32(uint32_t addr,uint32_t val){
    int s=sec_set_write_ptr(addr); if(s!=1) return s;
    s=sec_write_through(val); return s;
}
// Encoders from enable_vcn.py
static uint32_t enc_entry(uint32_t as_, uint32_t frame_bytes){ return (0x36 | (as_<<8) | ((frame_bytes/8)<<12)); }
static uint32_t enc_movi_n(uint32_t dest,uint32_t imm){ uint32_t v=imm&0x7F; uint32_t W=((v&0xF)<<12)|(dest<<8)|(((v>>4)&7)<<4)|0xC; return W; }
static uint32_t enc_call8(uint32_t pc,uint32_t tgt){ int32_t off=(int32_t)(tgt - ((pc+4)&~3))>>2; return (((off&0x3FFFF)<<6)|(2<<4)|5); }
#define THUNK_ADDR 0x0003ff00
#define FUN_SLOT_CLK 0x00023744
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(h==INVALID_HANDLE_VALUE){printf("FAIL open %u\n",GetLastError());return 1;}
 AMDBC250_IOCTL_INIT_HARDWARE ih;DWORD br; ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
 DeviceIoControl(h,IOCTL_INIT,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL);
 printf("=== VCN install --no-fire via DF (Q3 0x28/0x29) ===\n");
 // secure check: Q3 0x2A !=0xFD ?
 smnW_df(Q3_RSP,0); smnW_df(Q3_ARG,0x0115A870); smnW_df(Q3_CMD,0x2A);
 int st=-100; for(int i=0;i<2000;i++){ uint32_t s=smnR_df(Q3_RSP); if(s==0x01||s==0xFD){st=s;break;} Sleep(1);} uint32_t probe=smnR_df(Q3_ARG);
 printf("secure probe Q3 0x2A 0x0115A870: st 0x%X val 0x%08X %s\n",st,probe,(st==0x01)?"OPEN":(st==0xFD?"CLOSED":"?"));
 if(st!=0x01){printf("secure not open — unlock needed (skip)\n"); CloseHandle(h); return 0;}
 // dom6 snapshot
 printf("DOM6 snapshot:\n");
 uint32_t dom6_addrs[]={0x0006D190,0x0006D0F8,0x0006D17C,0x0006D184};
 const char* dom6_names[]={"STATUS","CTRL","CMD","RAIL"};
 for(int i=0;i<4;i++){ uint32_t v=smnR_df(dom6_addrs[i]); printf(" %s 0x%06X = 0x%08X\n",dom6_names[i],dom6_addrs[i],v); Sleep(5); }
 // build 20B sequence: entry a1,0x20; movi a10,0x16; call8 0x23744; *3; retw.n  (little endian packing)
 // Use enable_vcn.py build_sequence reference: 36 41 00 1c 6a e5 83 e3 ... total 20 bytes
 uint8_t seq[20]={0};
 // entry a1,0x20: 0x36 | (1<<8) | ((0x20/8)<<12) = 0x36 | 0x100 | 0x4000 = 0x4136 little 36 41 00
 seq[0]=0x36; seq[1]=0x41; seq[2]=0x00;
 int off=3;
 struct{uint32_t slot;} slots[3]={{0x16},{0x17},{0x18}};
 for(int s=0;s<3;s++){
   uint32_t pc=THUNK_ADDR+off;
   uint32_t movi=enc_movi_n(10, slots[s].slot);
   seq[off++] = movi &0xFF; seq[off++] = (movi>>8)&0xFF;
   uint32_t call=enc_call8(pc, FUN_SLOT_CLK);
   seq[off++] = call &0xFF; seq[off++] = (call>>8)&0xFF; seq[off++] = (call>>16)&0xFF;
 }
 seq[off++]=0x1d; seq[off++]=0xf0; // retw.n
 printf("Built seq %d bytes: ",off); for(int i=0;i<off;i++) printf("%02X ",seq[i]); printf("\n");
 if(off!=20){printf("seq len mismatch\n"); return 1;}
 // install: write 5 dwords to 0x3FF00 via smu_write32
 printf("Install to SMU SRAM 0x3FF00 (5 dwords) via DF Q3 0x28/0x29:\n");
 for(int i=0;i<20;i+=4){
   uint32_t dword = seq[i] | (seq[i+1]<<8) | (seq[i+2]<<16) | (seq[i+3]<<24);
   uint32_t addr = THUNK_ADDR + i;
   int s1=smu_write32(addr, dword);
   printf(" [%d] 0x%05X = 0x%08X -> Q3 0x28/0x29 st %d %s\n", i/4, addr, dword, s1, (s1==1?"OK":"FAIL"));
   if(s1!=1){printf("write failed, abort\n"); CloseHandle(h); return 1;}
   Sleep(10);
 }
 // repoint handler slot 0x776C
 uint32_t SLOT_FUNCPTR=0x776C;
 printf("Repoint Q3 0x61 handler slot 0x776C -> 0x3FF00:\n");
 int s2=smu_write32(SLOT_FUNCPTR, THUNK_ADDR);
 printf(" smu_write32 0x776C=0x3FF00 st %d %s\n", s2, (s2==1?"OK":"FAIL"));
 // verify by reading back via? Use Q3 0x2A cannot read SRAM, so try via SMN? 0x776C is SRAM not SMN, can't verify via SMN. Just report.
 printf("Install --no-fire done (no fire, handler slot set). On exit restore to 0 expected. Read-back via SMU SRAM not ported (would need Q2 0x0A transfer).\n");
 printf("To disarm: smu_write32 0x776C=0 + wipe 0x3FF00 (5 dwords 0)\n");
 int s3=smu_write32(SLOT_FUNCPTR, 0);
 printf("Disarm restore 0x776C=0 st %d\n", s3);
 for(int i=0;i<20;i+=4){ uint32_t addr=THUNK_ADDR+i; int s=smu_write32(addr,0); printf(" wipe 0x%05X st %d\n",addr,s); Sleep(10); }
 printf("Done --no-fire (safe)\n");
 CloseHandle(h); return 0;
}

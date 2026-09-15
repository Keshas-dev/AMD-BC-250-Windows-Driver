/* smu-freq-test.c â€” verify SMU frequency change works after init */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#define IOCTL_I 0x80000B80
#define IOCTL_R 0x80000B88
#define IOCTL_W 0x80000B8C
typedef struct{uint32_t off;uint32_t val;}RIO;
typedef struct{uint64_t base;uint32_t sz;uint32_t flags;uint64_t fb;uint32_t fbsz;}IHW;
static HANDLE g_h;
static DWORD g_le;
static BOOL W32(uint32_t o,uint32_t v){ RIO r={o,v}; DWORD br; BOOL ok=DeviceIoControl(g_h,IOCTL_W,&r,sizeof(r),&r,sizeof(r),&br,NULL); g_le=GetLastError(); if(!ok) printf(" W32[%X] gle=%lu\n",o,g_le); return ok; }
static uint32_t R32(uint32_t o){ RIO r={o,0}; DWORD br; if(!DeviceIoControl(g_h,IOCTL_R,&r,sizeof(r),&r,sizeof(r),&br,NULL)){g_le=GetLastError();return 0xDEADBEEF;} return r.val; }
static uint32_t SMR(uint32_t a){ W32(0x38,a); R32(0x38); return R32(0x3C); }
static BOOL SMW(uint32_t a,uint32_t v){ W32(0x38,a); R32(0x38); return W32(0x3C,v); }
static int WaitRsp(uint32_t a,int ms){ for(int i=0;i<ms;i++){uint32_t v=SMR(a); if(v==1||v==0xFF||v==0xFE||v==0xFD||v==0xFC) return (int)v; Sleep(1);} return -100; }
static int Q0(uint16_t m,uint32_t a,uint32_t*o){ SMW(0x03B10A68,0); SMW(0x03B10A48,a); SMW(0x03B10A08,m); int r=WaitRsp(0x03B10A68,2000); if(r==1&&o) *o=SMR(0x03B10A48); return r; }
static int Q3(uint16_t m,uint32_t a,uint32_t*o){ SMW(0x03B10A80,0); SMW(0x03B10A88,a); SMW(0x03B10A20,m); int r=WaitRsp(0x03B10A80,2000); if(r==1&&o) *o=SMR(0x03B10A88); return r; }
static int mv_to_vid(int mv){ return (int)((1.55-(double)mv/1000.0)/0.00625+0.5); }
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 g_h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(g_h==INVALID_HANDLE_VALUE){printf("FAIL %lu\n",GetLastError());return 1;}
 IHW ih={0xFE800000ULL,0x80000,1,0,0}; DWORD br; DeviceIoControl(g_h,IOCTL_I,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL);
 /* Baseline freq */
 uint32_t f0=0,v0=0; Q0(0x37,0,&f0); Q0(0x38,0,&v0);
 printf("Baseline: freq=%u MHz vid=%u (=%dmV)\n",f0,v0,(int)((-v0*0.00625+1.55)*1000));
 /* Core mask */
 uint32_t mask=SMR(0x0115A870);
 printf("Core mask 0x%X (cores=%u)\n",mask&0xFF,__popcnt(mask&0xFF));
 /* Governor sequence: temp 80C, unforce, profile 3, vid 950, freq 1600 */
 printf("Governor change to 1600MHz @ 950mV profile 3:\n");
 printf("  max_temp: r=%d\n",Q3(0x8C,80,NULL));
 printf("  unforce_freq: r=%d\n",Q0(0x3A,0,NULL));
 printf("  unforce_vid: r=%d\n",Q0(0x3C,0,NULL));
 int vid=mv_to_vid(950);
 printf("  set_perf_profile(3): r=%d\n",Q3(0x1E,3,NULL));
 Sleep(50);
 printf("  force_vid(%d): r=%d\n",vid,Q0(0x3B,vid,NULL));
 Sleep(50);
 printf("  force_freq(1600): r=%d\n",Q0(0x39,1600,NULL));
 Sleep(300);
 uint32_t f1=0,v1=0; Q0(0x37,0,&f1); Q0(0x38,0,&v1);
 printf("After: freq=%u MHz vid=%u (=%dmV)\n",f1,v1,(int)((-v1*0.00625+1.55)*1000));
 /* Restore: unforce */
 printf("Restore (unforce)...\n");
 Q0(0x3A,0,NULL); Q0(0x3C,0,NULL);
 Sleep(200);
 uint32_t f2=0; Q0(0x37,0,&f2);
 printf("After restore: freq=%u MHz\n",f2);
 return 0;
}


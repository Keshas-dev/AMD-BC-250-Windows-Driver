#include <windows.h>
#include <stdio.h>
int main() {
  HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
  if (h == INVALID_HANDLE_VALUE) { printf("No driver\n"); return 1; }
  UINT64 mm[4]={0xFE800000ULL,0x80000,0xC0000000ULL,0x40000000}; DWORD r;
  DeviceIoControl(h, (DWORD)0x80000B80, mm, sizeof(mm), NULL, 0, &r, NULL);
  
  UINT32 x[4]; x[0]=0x2004; x[1]=0;
  DeviceIoControl(h, (DWORD)0x80000B88, x, sizeof(x), x, sizeof(x), &r, NULL);
  printf("GRBM=0x%08X\n", x[1]);

  // Test CP register writes
  UINT32 cpOff[] = {0x2800,0x2810,0x2814,0x2818,0x281C,0x2820,0x2880,0x2890,0x28A0,0x28B0};
  for (int i=0; i<sizeof(cpOff)/4; i++) {
    x[0]=cpOff[i]; DeviceIoControl(h, (DWORD)0x80000B88, x, sizeof(x), x, sizeof(x), &r, NULL);
    UINT32 before = x[1];
    UINT32 w[4]={cpOff[i],0x12345678,0,0};
    DeviceIoControl(h, (DWORD)0x80000B8C, w, sizeof(w), NULL, 0, &r, NULL);
    x[0]=cpOff[i]; DeviceIoControl(h, (DWORD)0x80000B88, x, sizeof(x), x, sizeof(x), &r, NULL);
    UINT32 after = x[1];
    printf("CP[0x%04X]: 0x%08X -> 0x%08X %s\n", cpOff[i], before, after,
      (after==0x12345678) ? "WRITE OK!" : (before!=0xFFFFFFFF && after==before) ? "READ ONLY" : "BLOCKED");
    // Restore
    w[1]=before; DeviceIoControl(h, (DWORD)0x80000B8C, w, sizeof(w), NULL, 0, &r, NULL);
  }

  // Scratch test
  x[0]=0x2074; DeviceIoControl(h, (DWORD)0x80000B88, x, sizeof(x), x, sizeof(x), &r, NULL);
  printf("Scratch[0x2074]=0x%08X\n", x[1]);

  CloseHandle(h);
  return 0;
}

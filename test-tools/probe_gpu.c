#include <windows.h>
#include <stdio.h>
int main(){HANDLE h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);if(h==INVALID_HANDLE_VALUE)printf("AMDBC250DreamV43: FAIL %lu\n",GetLastError());else{printf("AMDBC250DreamV43: OK\n");CloseHandle(h);}return 0;}

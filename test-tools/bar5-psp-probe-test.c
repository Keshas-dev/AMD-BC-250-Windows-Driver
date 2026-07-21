#include <windows.h>
#include <stdio.h>
int main() {
    HANDLE h = CreateFileA("\\\\.\\AmdBcPsp", GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) printf("AmdBcPsp: FAIL (err=%lu) - our PSP proxy GONE\n", GetLastError());
    else { printf("AmdBcPsp: OK - our PSP proxy still alive\n"); CloseHandle(h); }
    HANDLE h2 = CreateFileA("\\\\.\\AMDPSP", GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h2 == INVALID_HANDLE_VALUE) printf("AMDPSP: FAIL (err=%lu) - MS device name differs\n", GetLastError());
    else { printf("AMDPSP: OK - MS PSP device present\n"); CloseHandle(h2); }
    return 0;
}

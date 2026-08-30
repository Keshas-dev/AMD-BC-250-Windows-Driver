#include <windows.h>
#include <stdio.h>

int main(void) {
    HANDLE h = CreateFileW(L"\\\\.\\AmdBcPsp", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("open fail %lu\n", GetLastError()); return 1; }

    struct { ULONG bus, dev, fn, off; const char* what; } t[] = {
        { 1, 0, 2, 0x00, "143E vendor/device" },
        { 1, 0, 2, 0x10, "143E BAR0 lo" },
        { 1, 0, 2, 0x14, "143E BAR0 hi" },
        { 1, 0, 2, 0x04, "143E CMD" },
        { 1, 0, 2, 0x0C, "143E class rev" },
        { 0, 0, 0, 0x00, "host bridge ven/dev" },
        { 1, 0, 0, 0x00, "B1D0F0 ven/dev" },
    };
    for (int i = 0; i < 7; i++) {
        ULONG in[3];
        in[0] = t[i].bus;
        in[1] = (t[i].dev << 3) | t[i].fn;
        in[2] = t[i].off;
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x222044UL, in, sizeof(in), in, sizeof(in), &ret, NULL);
        printf("%-22s B%lu:D%lu.F%lu off=0x%02X -> ok=%d val=0x%08X\n",
               t[i].what, t[i].bus, t[i].dev, t[i].fn, t[i].off, ok, in[0]);
    }
    CloseHandle(h);
    return 0;
}

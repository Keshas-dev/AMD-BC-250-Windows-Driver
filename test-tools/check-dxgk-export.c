#include <windows.h>
#include <stdio.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

int main(void) {
    /* Load dxgkrnl.sys as a data file to check exports */
    HMODULE h = LoadLibraryExA("dxgkrnl.sys", NULL, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE);
    if (!h) {
        printf("Cannot load dxgkrnl.sys (err=%lu)\n", GetLastError());
        return 1;
    }

    FARPROC addr = GetProcAddress(h, "DxgkInitialize");
    if (addr) {
        printf("DxgkInitialize FOUND at %p\n", addr);
    } else {
        printf("DxgkInitialize NOT EXPORTED (err=%lu)\n", GetLastError());
    }

    /* Also check other potential function names */
    FARPROC addr2 = GetProcAddress(h, "DxgkInitializeDisplayOnlyDriver");
    if (addr2) {
        printf("DxgkInitializeDisplayOnlyDriver FOUND at %p\n", addr2);
    } else {
        printf("DxgkInitializeDisplayOnlyDriver NOT FOUND\n");
    }

    FreeLibrary(h);
    return 0;
}

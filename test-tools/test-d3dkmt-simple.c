#include <windows.h>
#include <stdio.h>

int main(void) {
    FILE *g;
    fopen_s(&g, "C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\d3dkmt-simple.log", "w");
    if (!g) return 1;
    fprintf(g, "STEP 1: start\n"); fflush(g);

    HMODULE hd = LoadLibraryA("gdi32.dll");
    fprintf(g, "STEP 2: gdi32=%p\n", hd); fflush(g);

    /* Try getting D3DKMTEnumAdapters by ordinal */
    typedef NTSTATUS (WINAPI *pfnEnumAdapters)(void*);
    pfnEnumAdapters pEnum = (pfnEnumAdapters)GetProcAddress(hd, "D3DKMTEnumAdapters");
    fprintf(g, "STEP 3: D3DKMTEnumAdapters=%p\n", pEnum); fflush(g);

    if (pEnum) {
        UCHAR buf[4096] = {0};
        fprintf(g, "STEP 4: calling D3DKMTEnumAdapters\n"); fflush(g);
        NTSTATUS st = pEnum(buf);
        fprintf(g, "STEP 5: Status=0x%08X\n", st); fflush(g);
        /* Print first 64 bytes of result */
        UINT32 *dw = (UINT32*)buf;
        for (int i = 0; i < 16; i++) {
            fprintf(g, "  [%d]=0x%08X\n", i, dw[i]);
        }
        fflush(g);
    }

    fprintf(g, "STEP 6: done\n"); fflush(g);
    fclose(g);
    return 0;
}

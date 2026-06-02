#include <windows.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>

int main(void) {
    /* Write to console via low-level write to guarantee output */
    int fd;
    _pipe(&fd, NULL, 0); /* dummy */

    HANDLE hf = CreateFileA(
        "C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\minimal-test.log",
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        /* Try stderr */
        fprintf(stderr, "CreateFileA failed: %u\n", e);
        return 1;
    }

    char msg[] = "STEP 1: log opened\n";
    DWORD written;
    WriteFile(hf, msg, sizeof(msg)-1, &written, NULL);
    FlushFileBuffers(hf);

    /* Now try D3DKMT */
    HMODULE hg = GetModuleHandleA("gdi32.dll");
    if (!hg) hg = LoadLibraryA("gdi32.dll");
    char msg2[64];
    wsprintfA(msg2, "STEP 2: gdi32=%p\n", hg);
    WriteFile(hf, msg2, lstrlenA(msg2), &written, NULL);
    FlushFileBuffers(hf);

    if (hg) {
        FARPROC fp = GetProcAddress(hg, "D3DKMTEnumAdapters");
        wsprintfA(msg2, "STEP 3: EnumAdapters=%p\n", fp);
        WriteFile(hf, msg2, lstrlenA(msg2), &written, NULL);
        FlushFileBuffers(hf);

        if (fp) {
            wsprintfA(msg2, "STEP 4: about to call\n");
            WriteFile(hf, msg2, lstrlenA(msg2), &written, NULL);
            FlushFileBuffers(hf);

            typedef NTSTATUS (WINAPI *pfn)(void*);
            UCHAR buf[4096] = {0};
            NTSTATUS st = ((pfn)fp)(buf);
            wsprintfA(msg2, "STEP 5: Status=0x%08X\n", st);
            WriteFile(hf, msg2, lstrlenA(msg2), &written, NULL);
            FlushFileBuffers(hf);
        }
    }

    wsprintfA(msg2, "STEP 99: done\n");
    WriteFile(hf, msg2, lstrlenA(msg2), &written, NULL);
    FlushFileBuffers(hf);
    CloseHandle(hf);
    return 0;
}

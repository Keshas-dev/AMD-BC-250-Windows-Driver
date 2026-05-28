#include <windows.h>
#include <stdio.h>
int main() {
    printf("Test 1: Loading DLL...\n");
    HMODULE h = LoadLibraryW(L"amdbc250vulkan.dll");
    if (h) {
        printf("Test 2: DLL loaded at %p\n", h);
        FARPROC p = GetProcAddress(h, "vk_icdGetInstanceProcAddr");
        if (p) {
            printf("Test 3: vk_icdGetInstanceProcAddr found at %p\n", p);
        } else {
            printf("Test 3: FAIL - export not found\n");
        }
        FreeLibrary(h);
    } else {
        printf("Test 2: FAIL - error %lu\n", GetLastError());
    }
    printf("Done.\n");
    return 0;
}

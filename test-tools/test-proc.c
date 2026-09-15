#include <stdio.h>
typedef void* HMODULE;
typedef HMODULE (__stdcall *fnLoadLibraryA)(const char*);
typedef void* (__stdcall *fnGetProcAddress)(HMODULE, const char*);
int main() {
    fnLoadLibraryA LoadLibrary = (fnLoadLibraryA)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    fnGetProcAddress GetProcAddr = (fnGetProcAddress)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcAddress");
    HMODULE dll = LoadLibrary("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\amdradv64.dll");
    printf("LoadLibrary: %p\n", dll);
    if (!dll) return 1;
    void* fn = GetProcAddr(dll, "vkGetDeviceProcAddr");
    printf("vkGetDeviceProcAddr: %p\n", fn);
    fn = GetProcAddr(dll, "vkGetInstanceProcAddr");
    printf("vkGetInstanceProcAddr: %p\n", fn);
    fn = GetProcAddr(dll, "vkEnumeratePhysicalDevices");
    printf("vkEnumeratePhysicalDevices: %p\n", fn);
    fn = GetProcAddr(dll, "vkCreateInstance");
    printf("vkCreateInstance: %p\n", fn);
    return 0;
}

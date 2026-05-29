#include <windows.h>
#include <stdio.h>

int main() {
    FILE *f = fopen("output\\enum-test.txt", "w");
    fprintf(f, "=== Enum Test ===\n");

    HMODULE hDll = LoadLibraryW(L"output\\amdbc250vulkan.dll");
    if (!hDll) { fprintf(f, "Cannot load DLL\n"); fclose(f); return 1; }
    fprintf(f, "DLL loaded OK\n");

    void* pfnGetProc = GetProcAddress(hDll, "vk_icdGetInstanceProcAddr");
    fprintf(f, "vk_icdGetInstanceProcAddr = %p\n", pfnGetProc);

    if (pfnGetProc) {
        typedef void* (__stdcall *GetProcAddr)(void*, const char*);
        GetProcAddr gp = (GetProcAddr)pfnGetProc;
        
        void* fnEnum = gp(NULL, "vkEnumerateInstanceExtensionProperties");
        fprintf(f, "EnumerateInstanceExtProps = %p\n", fnEnum);
        
        void* fnCreate = gp(NULL, "vkCreateInstance");
        fprintf(f, "CreateInstance = %p\n", fnCreate);
        
        void* fnEnumPhys = gp(NULL, "vkEnumeratePhysicalDevices");
        fprintf(f, "EnumPhysicalDevices = %p\n", fnEnumPhys);

        void* fnCreateDev = gp(NULL, "vkCreateDevice");
        fprintf(f, "CreateDevice = %p\n", fnCreateDev);
        
        if (fnEnum) {
            typedef int (__stdcall *PFN_Enum)(const char*, unsigned int*, void*);
            unsigned int count = 99;
            int r = ((PFN_Enum)fnEnum)(NULL, &count, NULL);
            fprintf(f, "EnumExt result: %d count: %u\n", r, count);
        }
        
        if (fnCreate) {
            void* inst = NULL;
            typedef int (__stdcall *PFN_Create)(const void*, const void*, void*);
            int r = ((PFN_Create)fnCreate)(NULL, NULL, &inst);
            fprintf(f, "CreateInstance result: %d inst: %p\n", r, inst);
        }
    }

    fclose(f);
    FreeLibrary(hDll);
    return 0;
}

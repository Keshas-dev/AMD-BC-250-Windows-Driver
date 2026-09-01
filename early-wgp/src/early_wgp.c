/* early_wgp.c — BOOT_START driver for PCI 1022:143E that probes all SOS-gated regs before atikmdag and logs */
#include <ntddk.h>

#define GPU_BAR5_PHYS 0xFE800000ULL
#define GPU_BAR5_SIZE 0x80000
#define REG_GRBM_GFX_INDEX 0x34D0
#define REG_SPI_PG 0x5C3C
#define REG_CC_ARRAY 0x9C1C
#define REG_RLC_PG 0x3D64
#define REG_KIQ_SIZE 0xE068
#define REG_KIQ_BASE_LO 0xE060
#define REG_GFX_RB0_BASE 0x89E0
#define REG_SDMA_RB_BASE 0xE000
#define REG_SDMA_RB_CNTL 0xE008
#define REG_GCVM_PT_BASE 0xB608
#define REG_SCRATCH 0x32D4
#define REG_CP_MQD_BASE 0x9104
#define REG_RLC_SCHED 0xECA8

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;

static VOID LogReg(PCWSTR name, ULONG val){
    UNICODE_STRING path; RtlInitUnicodeString(&path, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\earlywgp");
    OBJECT_ATTRIBUTES oa; InitializeObjectAttributes(&oa,&path,OBJ_CASE_INSENSITIVE,NULL,NULL);
    HANDLE hKey=NULL;
    if(NT_SUCCESS(ZwCreateKey(&hKey,KEY_SET_VALUE,&oa,0,NULL,REG_OPTION_VOLATILE,NULL))){
        UNICODE_STRING vn; RtlInitUnicodeString(&vn,name);
        ZwSetValueKey(hKey,&vn,0,REG_DWORD,&val,sizeof(val));
        ZwClose(hKey);
    }
}
static VOID LogToFile(PCWSTR text){
    UNICODE_STRING path; RtlInitUnicodeString(&path, L"\\SystemRoot\\earlywgp.log");
    OBJECT_ATTRIBUTES oa; InitializeObjectAttributes(&oa,&path,OBJ_CASE_INSENSITIVE,NULL,NULL);
    HANDLE h=NULL; IO_STATUS_BLOCK ios;
    NTSTATUS st=ZwCreateFile(&h,FILE_APPEND_DATA,&oa,&ios,NULL,FILE_ATTRIBUTE_NORMAL,FILE_SHARE_READ|FILE_SHARE_WRITE,FILE_OPEN_IF,FILE_SYNCHRONOUS_IO_NONALERT,NULL,0);
    if(NT_SUCCESS(st)){
        ULONG len=(ULONG)wcslen(text)*2;
        ZwWriteFile(h,NULL,NULL,NULL,&ios,(PVOID)text,len,NULL,NULL);
        ZwClose(h);
    }
}
NTSTATUS DoEarlyProbe(void){
    PHYSICAL_ADDRESS pa; pa.QuadPart=GPU_BAR5_PHYS;
    PVOID va=MmMapIoSpace(pa,GPU_BAR5_SIZE,MmNonCached);
    if(!va){KdPrintEx((DPFLTR_IHVVIDEO_ID,DPFLTR_ERROR_LEVEL,"[EARLY-WGP] MmMapIoSpace FAILED\n")); return STATUS_INSUFFICIENT_RESOURCES;}
    PUCHAR bar=(PUCHAR)va;
    LogToFile(L"[EARLY-WGP] Boot probe start\r\n");
    KdPrintEx((DPFLTR_IHVVIDEO_ID,DPFLTR_INFO_LEVEL,"[EARLY-WGP] Boot probe start\n"));
    __try{
        static const ULONG bankSel[4]={0x00000000,0x00000100,0x00010000,0x00010100};
        for(int b=0;b<4;b++){
            WRITE_REGISTER_ULONG((PULONG)(bar+REG_GRBM_GFX_INDEX),bankSel[b]);
            WRITE_REGISTER_ULONG((PULONG)(bar+REG_CC_ARRAY),0x00000000);
            WRITE_REGISTER_ULONG((PULONG)(bar+REG_SPI_PG),0x0000001F);
            WRITE_REGISTER_ULONG((PULONG)(bar+REG_RLC_PG),0x0000001F);
        }
        WRITE_REGISTER_ULONG((PULONG)(bar+REG_GRBM_GFX_INDEX),0x15000000);
        struct {ULONG off; ULONG wval; PCWSTR name;} tests[]={
            {REG_SPI_PG,0x0000001F,L"SPI_PG"},
            {REG_CC_ARRAY,0x00000000,L"CC_ARRAY"},
            {REG_RLC_PG,0x0000001F,L"RLC_PG"},
            {REG_KIQ_SIZE,0x00000100,L"KIQ_SIZE"},
            {REG_KIQ_BASE_LO,0x12345000,L"KIQ_BASE_LO"},
            {REG_GFX_RB0_BASE,0x12345000,L"GFX_RB0_BASE"},
            {REG_SDMA_RB_BASE,0x12345000,L"SDMA_RB_BASE"},
            {REG_SDMA_RB_CNTL,0x00000040,L"SDMA_RB_CNTL"},
            {REG_GCVM_PT_BASE,0x12345000,L"GCVM_PT_BASE"},
            {REG_SCRATCH,0xA5A50000,L"SCRATCH"},
            {REG_CP_MQD_BASE,0x12345000,L"CP_MQD_BASE"},
            {REG_RLC_SCHED,0x000000A0,L"RLC_SCHED"},
        };
        for(int i=0;i<sizeof(tests)/sizeof(tests[0]);i++){
            ULONG before=READ_REGISTER_ULONG((PULONG)(bar+tests[i].off));
            WRITE_REGISTER_ULONG((PULONG)(bar+tests[i].off),tests[i].wval);
            ULONG after=READ_REGISTER_ULONG((PULONG)(bar+tests[i].off));
            ULONG isW = (after==tests[i].wval);
            ULONG isP = (after!=before && !isW);
            KdPrintEx((DPFLTR_IHVVIDEO_ID,DPFLTR_INFO_LEVEL,"[EARLY-WGP] %ls 0x%X before 0x%08X after 0x%08X %s\n", tests[i].name, tests[i].off, before, after, isW?"WRITABLE":isP?"PARTIAL":"STUCK"));
            LogReg(tests[i].name, after);
            // simple file log without swprintf
            WCHAR line[128]; line[0]=0;
            // use RtlString for file
            LogToFile(tests[i].name); LogToFile(L" probe\r\n");
        }
        for(int b=0;b<4;b++){
            WRITE_REGISTER_ULONG((PULONG)(bar+REG_GRBM_GFX_INDEX),bankSel[b]);
            ULONG v=READ_REGISTER_ULONG((PULONG)(bar+REG_SPI_PG));
            KdPrintEx((DPFLTR_IHVVIDEO_ID,DPFLTR_INFO_LEVEL,"[EARLY-WGP] BANK %d SPI_PG=0x%08X\n",b,v));
        }
        WRITE_REGISTER_ULONG((PULONG)(bar+REG_GRBM_GFX_INDEX),0x15000000);
        ULONG spiBc=READ_REGISTER_ULONG((PULONG)(bar+REG_SPI_PG));
        KdPrintEx((DPFLTR_IHVVIDEO_ID,DPFLTR_INFO_LEVEL,"[EARLY-WGP] BCAST SPI_PG=0x%08X\n",spiBc));
        LogReg(L"LastSpiPg",spiBc);
    }__except(EXCEPTION_EXECUTE_HANDLER){
        KdPrintEx((DPFLTR_IHVVIDEO_ID,DPFLTR_ERROR_LEVEL,"[EARLY-WGP] EXCEPTION\n"));
        LogToFile(L"EXCEPTION\r\n");
    }
    LogToFile(L"[EARLY-WGP] Boot probe done\r\n");
    MmUnmapIoSpace(va,GPU_BAR5_SIZE);
    return STATUS_SUCCESS;
}
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath){
    UNREFERENCED_PARAMETER(RegistryPath);
    KdPrintEx((DPFLTR_IHVVIDEO_ID,DPFLTR_INFO_LEVEL,"[EARLY-WGP] DriverEntry boot probe all regs\n"));
    DriverObject->DriverUnload=DriverUnload;
    DoEarlyProbe();
    return STATUS_SUCCESS;
}
VOID DriverUnload(PDRIVER_OBJECT DriverObject){
    UNREFERENCED_PARAMETER(DriverObject);
    KdPrintEx((DPFLTR_IHVVIDEO_ID,DPFLTR_INFO_LEVEL,"[EARLY-WGP] Unload\n"));
}

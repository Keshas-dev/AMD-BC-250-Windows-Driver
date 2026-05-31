#include <windows.h>
#include <stdio.h>

#define FILE_DEVICE_AMDBC250    0x8000
#define IOCTL_INDEX             0x800
#define CTL_CODE_AMDBC250(F, M, A) CTL_CODE(FILE_DEVICE_AMDBC250, IOCTL_INDEX + (F), M, A)

int main() {
    printf("=== User-mode CTL_CODE values ===\n");
    printf("INIT_HARDWARE (0x70): 0x%lX\n", CTL_CODE_AMDBC250(0x70, METHOD_BUFFERED, FILE_ANY_ACCESS));
    printf("SEND_PM4      (0x71): 0x%lX\n", CTL_CODE_AMDBC250(0x71, METHOD_BUFFERED, FILE_ANY_ACCESS));
    printf("READ_REG      (0x72): 0x%lX\n", CTL_CODE_AMDBC250(0x72, METHOD_BUFFERED, FILE_ANY_ACCESS));
    printf("WRITE_REG     (0x73): 0x%lX\n", CTL_CODE_AMDBC250(0x73, METHOD_BUFFERED, FILE_ANY_ACCESS));
    printf("GET_HW_STATUS (0x74): 0x%lX\n", CTL_CODE_AMDBC250(0x74, METHOD_BUFFERED, FILE_ANY_ACCESS));
    printf("READ_PCI_BAR  (0x75): 0x%lX\n", CTL_CODE_AMDBC250(0x75, METHOD_BUFFERED, FILE_ANY_ACCESS));
    printf("\n=== KMD switch case values ===\n");
    printf("INIT_HARDWARE: 0x80000B80\n");
    printf("SEND_PM4:      0x80000B84\n");
    printf("READ_REG:      0x80000B88\n");
    printf("WRITE_REG:     0x80000B8C\n");
    printf("GET_HW_STATUS: 0x80000B90\n");
    printf("READ_PCI_BAR:  0x80000B94\n");
    printf("\n=== Match check ===\n");
    DWORD codes[] = {
        CTL_CODE_AMDBC250(0x70, METHOD_BUFFERED, FILE_ANY_ACCESS),
        CTL_CODE_AMDBC250(0x71, METHOD_BUFFERED, FILE_ANY_ACCESS),
        CTL_CODE_AMDBC250(0x72, METHOD_BUFFERED, FILE_ANY_ACCESS),
        CTL_CODE_AMDBC250(0x73, METHOD_BUFFERED, FILE_ANY_ACCESS),
        CTL_CODE_AMDBC250(0x74, METHOD_BUFFERED, FILE_ANY_ACCESS),
        CTL_CODE_AMDBC250(0x75, METHOD_BUFFERED, FILE_ANY_ACCESS),
    };
    DWORD kmd[] = { 0x80000B80, 0x80000B84, 0x80000B88, 0x80000B8C, 0x80000B90, 0x80000B94 };
    const char *names[] = { "INIT", "PM4", "READ", "WRITE", "HWSTATUS", "PCIBAR" };
    for (int i = 0; i < 6; i++) {
        printf("%s: user=0x%lX  kmd=0x%lX  %s\n", names[i], codes[i], kmd[i], 
               codes[i] == kmd[i] ? "MATCH" : "MISMATCH!");
    }
    return 0;
}

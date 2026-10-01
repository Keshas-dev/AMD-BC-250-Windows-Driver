#include "patches.h"
#include "smu.h"
#include "patches.data.h"
#include "msvc_compat.h"

EFI_STATUS apply_smu_patches(EFI_SYSTEM_TABLE *SystemTable) {
    EFI_PHYSICAL_ADDRESS dma_phys = 0;
    EFI_STATUS status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiBootServicesData, 1, &dma_phys);
    if (EFI_ERROR(status)) {
        print(SystemTable, L"Error: Failed to allocate DMA page for SMU patching!\r\n");
        return status;
    }

    for (unsigned int i = 0; i < NUM_PATCHES; i++) {
        const smu_patch_t *patch = &g_smu_patches[i];
        unsigned char cur[32];

        int st = smu_read_bytes(SystemTable, patch->addr, patch->len, cur, dma_phys);
        if (st != 0x01) {
            print(SystemTable, L"Error: Failed to read SMU SRAM at ");
            print_hex(SystemTable, patch->addr);
            print(SystemTable, L"\r\n");
            SystemTable->BootServices->FreePages(dma_phys, 1);
            return EFI_DEVICE_ERROR;
        }

        int match = 1;
        for (unsigned int j = 0; j < patch->len; j++) {
            if (cur[j] != patch->data[j]) {
                match = 0;
                break;
            }
        }

        if (!match) {
            st = smu_write_bytes(SystemTable, patch->addr, patch->len, patch->data, dma_phys);
            if (st != 0x01) {
                print(SystemTable, L"Error: Failed to write SMU SRAM patch at ");
                print_hex(SystemTable, patch->addr);
                print(SystemTable, L"\r\n");
                SystemTable->BootServices->FreePages(dma_phys, 1);
                return EFI_DEVICE_ERROR;
            }

            // Verify write
            st = smu_read_bytes(SystemTable, patch->addr, patch->len, cur, dma_phys);
            if (st != 0x01) {
                print(SystemTable, L"Error: Failed to re-read SMU SRAM patch at ");
                print_hex(SystemTable, patch->addr);
                print(SystemTable, L"\r\n");
                SystemTable->BootServices->FreePages(dma_phys, 1);
                return EFI_DEVICE_ERROR;
            }

            for (unsigned int j = 0; j < patch->len; j++) {
                if (cur[j] != patch->data[j]) {
                    print(SystemTable, L"Error: SMU SRAM patch verification failed at ");
                    print_hex(SystemTable, patch->addr);
                    print(SystemTable, L"\r\n");
                    SystemTable->BootServices->FreePages(dma_phys, 1);
                    return EFI_DEVICE_ERROR;
                }
            }
        }
    }

    SystemTable->BootServices->FreePages(dma_phys, 1);
    return EFI_SUCCESS;
}

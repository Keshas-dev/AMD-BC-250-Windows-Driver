/*
 * elog.h - file logging for the BC-250 UEFI probe.
 */
#ifndef ELOG_H
#define ELOG_H

#include <efi.h>

/*
 * Creates an empty name (ASCII, 8.3-safe) on the volume the probe booted from.
 * Returns 1 on success. Everything printed afterwards is teed to both the
 * console and that file. If this fails the console output is unaffected, so
 * elog_str still works and a file-system problem is never silent.
 */
int elog_open(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, const char *name);

void elog_header(EFI_SYSTEM_TABLE *st);
void elog_close(void);

void elog_str(EFI_SYSTEM_TABLE *st, const char *s);
void elog_u32(EFI_SYSTEM_TABLE *st, unsigned int v);
void elog_hex32(EFI_SYSTEM_TABLE *st, unsigned int v);
void elog_hex64(EFI_SYSTEM_TABLE *st, unsigned long long v);

#endif /* ELOG_H */

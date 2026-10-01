/*
 * elog.h - console + file logging for the BC-250 UEFI probe.
 *
 * Everything printed to the console is also appended to a text file on every
 * writable EFI file system, so a long result set can be read off the USB stick
 * instead of photographed. The filename is WGPLOG.TXT, deliberately 8.3-safe so
 * it works on FAT32 without long-name support.
 *
 * Output is teed: if no file system is writable the probe still prints normally.
 */
#ifndef ELOG_H
#define ELOG_H

#include <efi.h>

/* Opens the log on every file system it can find. Safe to call once. */
void elog_init(EFI_SYSTEM_TABLE *st, EFI_HANDLE image);

/* Flushes and closes. Call before halting so nothing is lost. */
void elog_close(EFI_SYSTEM_TABLE *st);

void elog_str(EFI_SYSTEM_TABLE *st, const char *s);
void elog_u32(EFI_SYSTEM_TABLE *st, unsigned int v);
void elog_hex32(EFI_SYSTEM_TABLE *st, unsigned int v);
void elog_hex64(EFI_SYSTEM_TABLE *st, unsigned long long v);

/* A short header: firmware timestamp, and how many log files were opened. */
void elog_banner(EFI_SYSTEM_TABLE *st);

#endif /* ELOG_H */

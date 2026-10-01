/*
 * wgp.h - WGP unlock experiment for the BC-250, run in the UEFI phase.
 */
#ifndef WGP_H
#define WGP_H

#include <efi.h>

/* Runs the whole experiment and prints results to the UEFI console. */
void wgp_probe(EFI_SYSTEM_TABLE *SystemTable);

#endif /* WGP_H */

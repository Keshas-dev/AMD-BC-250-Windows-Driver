/*
 * early.c - a deliberately independent, minimal "did we run at all" check.
 *
 * The probe produced no console output at all, which cannot be distinguished
 * from "the firmware never called efi_main" by looking at the screen. So the
 * first thing efi_main does is create one small file with the simplest code
 * path available.
 *
 * This duplicates the file-system protocol declarations on purpose. If the
 * layout in elog.c is wrong, this must still work, otherwise the two failures
 * would mask each other. It only ever uses Open, Write, SetPosition, Flush and
 * Close.
 */

#include <efi.h>
#include "early.h"

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_MODE_READ     0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE    0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE   0x8000000000000000ULL

typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_FILE_PROTOCOL *(*EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *This, CHAR16 *Name,
                                            UINT64 OpenMode, EFI_FILE_PROTOCOL **Handle);
typedef EFI_STATUS (*EFI_FILE_SIMPLE)(EFI_FILE_PROTOCOL *This);
typedef EFI_STATUS (*EFI_FILE_WRITE)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
typedef EFI_STATUS (*EFI_FILE_SETPOS)(EFI_FILE_PROTOCOL *This, UINT64 Position);

struct _EFI_FILE_PROTOCOL {
    UINT64             Revision;
    EFI_FILE_OPEN      Open;
    EFI_FILE_SIMPLE    Close;
    EFI_FILE_SIMPLE    Delete;
    EFI_FILE_SIMPLE    Read;
    EFI_FILE_WRITE     Write;
    EFI_FILE_SIMPLE    GetPosition;
    EFI_FILE_SETPOS    SetPosition;
    EFI_FILE_SIMPLE    GetInfo;
    EFI_FILE_SIMPLE    SetInfo;
    EFI_FILE_SIMPLE    Flush;
};

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64               Revision;
    EFI_FILE_PROTOCOL *(*OpenVolume)(VOID *This, EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    UINT64                   Revision;
    EFI_HANDLE               ImageHandle;
    EFI_HANDLE               DeviceHandle;
    VOID                    *DevicePath;
    VOID                    *Reserved;
    UINT32                   LoadOptionsSize;
    VOID                    *LoadOptions;
    VOID                    *ImageBase;
    UINT64                   ImageSize;
    UINT32                   ImageCodeType;
    UINT32                   ImageDataType;
    UINT64                   Unload;
} EARLY_LOADED_IMAGE;

#ifndef EFI_ERROR
#define EFI_ERROR(status) (((UINT64)(status) & 0x8000000000000000ULL) != 0)
#endif

/* Returns 1 if the marker file was created, 0 if not. Also reports which step
 * failed through *stage so the caller can print it. */
int early_marker(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, int *stage)
{
    EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EARLY_LOADED_IMAGE *li = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0, *f = 0;
    const char msg[] = "EFIRUN: efi_main was reached\r\n";
    UINTN n = sizeof(msg) - 1;
    int rc = 0;

    *stage = 0;

    if (EFI_ERROR(st->BootServices->HandleProtocol(image, &li_guid, (VOID **)&li)) || !li)
        return 0;
    *stage = 1;

    if (EFI_ERROR(st->BootServices->HandleProtocol(li->DeviceHandle, &fs_guid, (VOID **)&fs)) || !fs)
        return 0;
    *stage = 2;

    if (EFI_ERROR(fs->OpenVolume(fs, &root)) || !root)
        return 0;
    *stage = 3;

    f = root->Open(root, (CHAR16 *)L"EFIRUN.TXT",
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, &f);
    *stage = 4;
    if (f) {
        f->SetPosition(f, 0);
        f->Write(f, &n, (VOID *)msg);
        f->Flush(f);
        f->Close(f);
        rc = 1;
    }
    *stage = 5;

    root->Close(root);
    return rc;
}

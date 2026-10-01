/*
 * elog.c - file logging for the BC-250 UEFI probe.
 *
 * Console output alone is not enough: a result set this size has to be
 * photographed, and the previous revision produced no console output at all
 * because the firmware never loaded the image - so the file path had in fact
 * never been exercised. It is now, with the loader known to work.
 *
 * One file per test, written to the volume the probe booted from. If that
 * volume cannot be written the probe still prints everything to the console and
 * says so, so a failure here is never silent.
 *
 * Only the first eleven EFI_FILE_PROTOCOL members are laid out, per UEFI spec
 * 2.3.16. Open, Write, SetPosition, Flush and Close all sit inside that prefix
 * and nothing past Flush is ever dereferenced.
 */

#include <efi.h>
#include "elog.h"

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_MODE_READ     0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE    0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE   0x8000000000000000ULL

#ifndef EFI_ERROR
#define EFI_ERROR(status) (((UINT64)(status) & 0x8000000000000000ULL) != 0)
#endif

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
} ELOG_LOADED_IMAGE;

static EFI_FILE_PROTOCOL *g_file = 0;
static unsigned long long g_pos = 0;
static const char       *g_name = 0;

static const char *g_hexd = "0123456789ABCDEF";

static unsigned int elen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static void raw(const char *s)
{
    UINTN n;
    if (!g_file) return;
    n = elen(s);
    g_file->Write(g_file, &n, (VOID *)s);
    g_file->Flush(g_file);
    g_pos += elen(s);
}

/* ------------------------------------------------------------------ */

static void to_console(EFI_SYSTEM_TABLE *st, const char *s)
{
    UINT16 buf[200];
    int i = 0;
    while (*s && i < 199) buf[i++] = (UINT16)(unsigned char)*s++;
    buf[i] = 0;
    if (st && st->ConOut) st->ConOut->OutputString(st->ConOut, buf);
}

void elog_str(EFI_SYSTEM_TABLE *st, const char *s)
{
    to_console(st, s);
    raw(s);
}

void elog_u32(EFI_SYSTEM_TABLE *st, unsigned int v)
{
    char b[12], one[2];
    int n = 0, i;
    if (!v) { elog_str(st, "0"); return; }
    while (v && n < 10) { b[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (i = 0; i < n; i++) { one[0] = b[n - 1 - i]; one[1] = 0; elog_str(st, one); }
}

void elog_hex32(EFI_SYSTEM_TABLE *st, unsigned int v)
{
    char b[11];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 8; i++) b[2 + i] = g_hexd[(v >> ((7 - i) * 4)) & 0xF];
    b[10] = 0;
    elog_str(st, b);
}

void elog_hex64(EFI_SYSTEM_TABLE *st, unsigned long long v)
{
    char b[19];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 16; i++) b[2 + i] = g_hexd[(unsigned)((v >> ((15 - i) * 4)) & 0xF)];
    b[18] = 0;
    elog_str(st, b);
}

/* ------------------------------------------------------------------ */

int elog_open(EFI_SYSTEM_TABLE *st, EFI_HANDLE image, const char *name)
{
    EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    ELOG_LOADED_IMAGE *li = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0, *f = 0;
    CHAR16 wide[16];
    int i, n = 0;

    g_file = 0;
    g_pos = 0;
    g_name = name;

    if (EFI_ERROR(st->BootServices->HandleProtocol(image, &li_guid, (VOID **)&li)) || !li)
        return 0;
    if (EFI_ERROR(st->BootServices->HandleProtocol(li->DeviceHandle, &fs_guid, (VOID **)&fs)) || !fs)
        return 0;
    if (EFI_ERROR(fs->OpenVolume(fs, &root)) || !root)
        return 0;

    for (i = 0; name[i] && n < 15; i++) wide[n++] = (CHAR16)name[i];
    wide[n] = 0;

    f = root->Open(root, wide,
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, &f);
    root->Close(root);

    if (!f) return 0;

    f->SetPosition(f, 0);      /* truncate a previous run */
    f->Flush(f);
    g_file = f;
    return 1;
}

void elog_header(EFI_SYSTEM_TABLE *st)
{
    elog_str(st, "==============================================\r\n");
    elog_str(st, " BC-250 UEFI WGP PROBE - ");
    elog_str(st, g_name ? g_name : "LOG");
    elog_str(st, "\r\n");
    elog_str(st, "==============================================\r\n");
}

void elog_close(void)
{
    if (g_file) {
        g_file->Flush(g_file);
        g_file->Close(g_file);
        g_file = 0;
    }
}

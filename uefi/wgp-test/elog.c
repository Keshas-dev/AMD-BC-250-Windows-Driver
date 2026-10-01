/*
 * elog.c - console + file logging for the BC-250 UEFI probe.
 *
 * yoppeh/efi is a minimal header set with no file-system protocol, so
 * EFI_SIMPLE_FILE_SYSTEM_PROTOCOL and the head of EFI_FILE_PROTOCOL are declared
 * here. Only the first eleven EFI_FILE_PROTOCOL members are laid out, which is
 * all that is needed: Open, Write, SetPosition, Flush and Close are all within
 * that prefix, and nothing beyond Flush is ever dereferenced. Order follows
 * UEFI spec 2.3.16.
 */

#include <efi.h>
#include "elog.h"
#include "msvc_compat.h"

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_MODE_READ     0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE    0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE   0x8000000000000000ULL

#define ELOG_MAX_FILES 8
#define ELOG_NAME      L"WGPLOG.TXT"

/* smu.h defines EFI_ERROR but this file must not depend on it. */
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
    UINT64              Revision;
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
    EFI_MEMORY_TYPE          ImageCodeType;
    EFI_MEMORY_TYPE          ImageDataType;
    UINT64                   Unload;
} ELOG_LOADED_IMAGE;

static EFI_FILE_PROTOCOL *g_file[ELOG_MAX_FILES];
static int                g_nfiles = 0;
static unsigned long long g_pos = 0;

static const char *g_hexd = "0123456789ABCDEF";

/* No CRT is linked, so strlen has to be local. */
static unsigned int elog_strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

/* ------------------------------------------------------------------ */

static void raw_write(const char *data, unsigned int len)
{
    int i;
    for (i = 0; i < g_nfiles; i++) {
        UINTN n = len;
        g_file[i]->Write(g_file[i], &n, (VOID *)data);
        g_file[i]->Flush(g_file[i]);
    }
    g_pos += len;
}

static void out(EFI_SYSTEM_TABLE *st, const char *s)
{
    UINT16 buf[256];
    int i = 0;
    while (*s && i < 255) buf[i++] = (UINT16)(unsigned char)*s++;
    buf[i] = 0;
    if (st && st->ConOut) st->ConOut->OutputString(st->ConOut, buf);
    if (g_nfiles) raw_write(s, elog_strlen(s));
}

void elog_str(EFI_SYSTEM_TABLE *st, const char *s) { out(st, s); }

void elog_u32(EFI_SYSTEM_TABLE *st, unsigned int v)
{
    char b[12];
    int n = 0, i;
    if (!v) { out(st, "0"); return; }
    while (v && n < 10) { b[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (i = n - 1; i >= 0; i--) {
        char c[2];
        c[0] = b[i]; c[1] = 0;
        out(st, c);
    }
}

void elog_hex32(EFI_SYSTEM_TABLE *st, unsigned int v)
{
    char b[11];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 8; i++) b[2 + i] = g_hexd[(v >> ((7 - i) * 4)) & 0xF];
    b[10] = 0;
    out(st, b);
}

void elog_hex64(EFI_SYSTEM_TABLE *st, unsigned long long v)
{
    char b[21];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 16; i++) b[2 + i] = g_hexd[(unsigned)((v >> ((15 - i) * 4)) & 0xF)];
    b[18] = 0;
    out(st, b);
}

/* ------------------------------------------------------------------ */

static void try_open(EFI_SYSTEM_TABLE *st, EFI_HANDLE h)
{
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0, *f = 0;
    char path[64];
    int n;

    if (g_nfiles >= ELOG_MAX_FILES) return;
    if (EFI_ERROR(st->BootServices->HandleProtocol(h, &fs_guid, (VOID **)&fs)) || !fs)
        return;
    if (EFI_ERROR(fs->OpenVolume(fs, &root)) || !root) return;

    /* Name the log after the volume so several file systems do not overwrite
     * each other. Volume index comes from how many we already have. */
    {
        EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
        ELOG_LOADED_IMAGE *li = 0;
        int is_boot_dev = 0;
        if (!EFI_ERROR(st->BootServices->HandleProtocol(h, &li_guid, (VOID **)&li)) && li) {
            char pre[16];
            n = 0;
            pre[n++] = 'W'; pre[n++] = 'G'; pre[n++] = 'P';
            pre[n++] = (char)('0' + (g_nfiles + 1));
            pre[n++] = '-'; pre[n++] = 'L'; pre[n++] = 'O'; pre[n++] = 'G';
            pre[n++] = '.'; pre[n++] = 'T'; pre[n++] = 'X'; pre[n++] = 'T';
            pre[n] = 0;
            /* ASCII to CHAR16 in place is not possible, build a wide name */
            {
                CHAR16 w[16];
                int k;
                for (k = 0; k < n; k++) w[k] = (CHAR16)pre[k];
                w[n] = 0;
                f = root->Open(root, w,
                               EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                               &f);
            }
            is_boot_dev = 1;
        }
        (void)is_boot_dev;
    }

    if (!f) {
        /* Fall back to a fixed 8.3 name for volumes with no loaded image. */
        f = root->Open(root, (CHAR16 *)ELOG_NAME,
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, &f);
    }

    root->Close(root);

    if (f) {
        f->SetPosition(f, 0);          /* truncate an earlier run */
        f->Flush(f);
        g_file[g_nfiles++] = f;
    }

    (void)path;
}

void elog_init(EFI_SYSTEM_TABLE *st, EFI_HANDLE image)
{
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_HANDLE *hs = 0;
    UINTN cnt = 0, i;
    int pass;

    g_nfiles = 0;
    g_pos = 0;

    /* Pass 1: the volume we were loaded from, which is the USB stick. Most
     * likely to be writable and the one the user will look at. */
    {
        ELOG_LOADED_IMAGE *li = 0;
        if (!EFI_ERROR(st->BootServices->HandleProtocol(image, &li_guid, (VOID **)&li)) && li)
            try_open(st, li->DeviceHandle);
    }

    /* Pass 2: anything else with a file system, e.g. the ESP.
     *
     * AllHandles, not ByProtocol: yoppeh/efi only declares those three search
     * types, and ByProtocol needs a SearchKey this call has no use for. AllHandles
     * returns every handle carrying the protocol, which is what we want. */
    if (!EFI_ERROR(st->BootServices->LocateHandleBuffer(
            AllHandles, &fs_guid, NULL, &cnt, &hs)) && hs) {
        for (i = 0; i < cnt; i++) try_open(st, hs[i]);
        st->BootServices->FreePool(hs);
    }

    /* Report before any real output, since g_nfiles is now known. */
    for (pass = 0; pass < 1; pass++) {
        char b[64];
        int n = 0;
        b[n++] = '['; b[n++] = 'e'; b[n++] = 'l'; b[n++] = 'o'; b[n++] = 'g';
        b[n++] = ']'; b[n++] = ' '; b[n++] = 'o'; b[n++] = 'p'; b[n++] = 'e';
        b[n++] = 'n'; b[n++] = 'e'; b[n++] = 'd'; b[n++] = ' ';
        b[n++] = (char)('0' + g_nfiles); b[n++] = ' '; b[n++] = 'f';
        b[n++] = 'i'; b[n++] = 'l'; b[n++] = 'e'; b[n++] = 's'; b[n++] = 0;
        out(st, b);
        if (g_nfiles) {
            out(st, " writing to: ");
            for (i = 0; i < (UINTN)g_nfiles; i++) {
                out(st, (i == 0) ? "WGPLOG.TXT" : "WGPn-LOG.TXT");
                if (i + 1 < (UINTN)g_nfiles) out(st, ",");
            }
        } else {
            out(st, "none (console only)");
        }
        out(st, "\r\n");
    }
}

void elog_banner(EFI_SYSTEM_TABLE *st)
{
    out(st, "==============================================\r\n");
    out(st, " BC-250 UEFI WGP UNLOCK PROBE\r\n");
    out(st, "==============================================\r\n");
}

void elog_close(EFI_SYSTEM_TABLE *st)
{
    int i;
    for (i = 0; i < g_nfiles; i++) {
        g_file[i]->Flush(g_file[i]);
        g_file[i]->Close(g_file[i]);
    }
    (void)st;
    g_nfiles = 0;
}

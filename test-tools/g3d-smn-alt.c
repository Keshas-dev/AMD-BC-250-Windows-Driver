/*
 * g3d-smn-alt - is B0:D18:F2 (1022:13F2) a second SMN requester?
 *
 * We reach SMN today through 00:00.0 config 0xB8/0xBC. The PS5 Linux loader
 * instead reaches GPU memory registers through ECAM at 00:18.2, and that
 * function's config space has a real index/data-looking pair at 0x80/0x84
 * (0x80=0x0000001C, 0x84=0). On AMD APU dies 1022:13F0-13F7 are the NBI /
 * data-fabric functions, so this one may be a second path onto the fabric.
 *
 * If the ACL that blocks SPI_PG and the ring block is per-requester, a second
 * requester could see different permissions.
 *
 * The existing PCI_SMN IOCTL always uses config offsets 0xB8/0xBC but takes a
 * BDF, so it can target this function directly - no driver change needed.
 *
 * READ-ONLY: this only performs SMN reads. No writes anywhere.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_PCI_SMN 0x80000C30

typedef struct {
    uint32_t SmnAddress, SmnData, IsWrite, Result;
    uint32_t Bus, Device, Function, Method, Bar5SmnData;
} PCI_SMN;

static HANDLE g_h;

static BOOL SmnRead(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t addr, uint32_t *out, uint32_t *bar5) {
    PCI_SMN p; DWORD br = 0;
    memset(&p, 0, sizeof(p));
    p.SmnAddress = addr; p.IsWrite = 0;
    p.Bus = bus; p.Device = dev; p.Function = fn;
    if (!DeviceIoControl(g_h, IOCTL_PCI_SMN, &p, sizeof(p), &p, sizeof(p), &br, NULL))
        return FALSE;
    if (out)  *out  = p.SmnData;
    if (bar5) *bar5 = p.Bar5SmnData;
    return p.Result == 1;
}

typedef struct { uint32_t b, d, f; const char *name; } BDF;

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("FAIL: %lu\n", GetLastError()); return 1; }

    /* Known-good references. */
    const uint32_t addrs[] = {
        0x0115A870,   /* core presence mask: expect 0xFF on 8 cores */
        0x0005A870,   /* alias, also 0xFF per earlier notes */
        0x03B10A20,   /* SMU queue 3 command register - expect non-trivial */
    };
    const char *aname[] = { "core mask 0x0115A870", "alias  0x0005A870", "SMU Q3 cmd 0x03B10A20" };

    const BDF targets[] = {
        { 0, 0x00, 0, "00:00.0  ROOT COMPLEX  (known working)" },
        { 0, 0x18, 0, "00:18.0  1022:7807  NBI / USB" },
        { 0, 0x18, 1, "00:18.1" },
        { 0, 0x18, 2, "00:18.2  1022:13F2  <-- PS5 loader target" },
        { 0, 0x18, 3, "00:18.3" },
    };

    for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); t++) {
        printf("\n=== B%d:D%02X:F%d  %s ===\n",
               targets[t].b, targets[t].d, targets[t].f, targets[t].name);
        for (unsigned a = 0; a < sizeof(addrs) / sizeof(addrs[0]); a++) {
            uint32_t v = 0, b5 = 0;
            BOOL ok = SmnRead(targets[t].b, targets[t].d, targets[t].f, addrs[a], &v, &b5);
            printf("  %-22s -> %s  cfg=0x%08X  bar5=0x%08X\n",
                   aname[a], ok ? "ok  " : "FAIL", v, b5);
        }
    }

    /* Direct comparison: does 00:18.2 at least hold a distinct value in the
     * data port when nothing is being requested? */
    printf("\n=== data-port idle value (SmnAddress=0) ===\n");
    for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); t++) {
        uint32_t v = 0;
        SmnRead(targets[t].b, targets[t].d, targets[t].f, 0, &v, NULL);
        printf("  B%d:D%02X:F%d  data port = 0x%08X\n",
               targets[t].b, targets[t].d, targets[t].f, v);
    }

    printf("\nDone (read-only, no writes issued).\n");
    CloseHandle(g_h);
    return 0;
}
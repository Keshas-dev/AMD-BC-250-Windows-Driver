using System;
using System.Runtime.InteropServices;

class CpProbe2Test {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr CreateFileW(string n, uint a, uint s, IntPtr p, uint c, uint f, IntPtr t);
    [DllImport("kernel32.dll")]
    static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inb, uint inl, byte[] outb, uint outl, out uint br, IntPtr ov);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const uint RR = 0x80000B88, WR = 0x80000B8C, INIT = 0x80000B80;

    static uint ReadReg(IntPtr h, uint off) {
        byte[] ib = new byte[8], ob = new byte[8];
        BitConverter.GetBytes(off).CopyTo(ib, 0); uint br;
        if (!DeviceIoControl(h, RR, ib, 8, ob, 8, out br, IntPtr.Zero)) return 0xFFFFFFFF;
        return BitConverter.ToUInt32(ob, 4);
    }
    static void WriteReg(IntPtr h, uint off, uint val) {
        byte[] ib = new byte[8];
        BitConverter.GetBytes(off).CopyTo(ib, 0);
        BitConverter.GetBytes(val).CopyTo(ib, 4); uint br;
        DeviceIoControl(h, WR, ib, 8, null, 0, out br, IntPtr.Zero);
    }

    static void Probe(IntPtr h, uint off, string name, uint testVal) {
        uint before = ReadReg(h, off);
        WriteReg(h, off, testVal);
        uint after = ReadReg(h, off);
        bool writable = (after != before);
        if (writable) WriteReg(h, off, before); // restore
        Console.WriteLine("  {0,-20} [0x{1:X6}] 0x{2:X8} -> 0x{3:X8}  [{4}]",
            name, off, before, after, writable ? "WRITABLE" : (before == 0xFFFFFFFF ? "UNMAPPED" : "readonly"));
    }

    static void Main() {
        IntPtr h = CreateFileW(@"\\.\AMDBC250DreamV43", 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h == (IntPtr)(-1)) { Console.WriteLine("Open: " + Marshal.GetLastWin32Error()); return; }
        byte[] b = new byte[32];
        BitConverter.GetBytes((ulong)0xFE800000).CopyTo(b, 0);
        BitConverter.GetBytes((ulong)0x80000).CopyTo(b, 8);
        BitConverter.GetBytes((ulong)0xC0000000).CopyTo(b, 16);
        BitConverter.GetBytes((ulong)0x40000000).CopyTo(b, 24); uint br;
        DeviceIoControl(h, INIT, b, 32, null, 0, out br, IntPtr.Zero);
        Console.WriteLine("=== Extended CP Probe ===\n");

        uint pat = 0xDEADBEEF;

        // Compute ring at GC_BASE shift: 0x1260 + 0xC900 = 0xDB60
        Console.WriteLine("COMPUTE RING0 (GC_BASE shifted):");
        Probe(h, 0xDB60, "COMPUTE_BASE_LO", pat);
        Probe(h, 0xDB64, "COMPUTE_BASE_HI", pat);
        Probe(h, 0xDB68, "COMPUTE_CNTL", pat);
        Probe(h, 0xDB6C, "COMPUTE_RPTR", pat);
        Probe(h, 0xDB78, "COMPUTE_WPTR", pat);

        // GC_BASE shifted CP ring (re-verify with write back)
        Console.WriteLine("\nCP RING0 re-verify (GC_BASE shifted):");
        Probe(h, 0xDA60, "RING0_BASE_LO", pat);
        Probe(h, 0xDA68, "RING0_CNTL", pat);
        Probe(h, 0xDA6C, "RING0_RPTR", pat);
        Probe(h, 0xDA78, "RING0_WPTR", pat);

        // Try aligned address for BASE_LO
        Console.WriteLine("\nBASE_LO with valid-looking addresses:");
        Probe(h, 0xDA60, "BASE_LO=0x10000", 0x00010000);
        Probe(h, 0xDA60, "BASE_LO=0xFC000000", 0xFC000000);
        Probe(h, 0xDA60, "BASE_LO=0xFFE00000", 0xFFE00000);
        // restore original after tests
        WriteReg(h, 0xDA60, 0);
        // Try BASE_HI
        Probe(h, 0xDA64, "BASE_HI=1", 1);

        // Check COMPUTE registers at native NBIO
        Console.WriteLine("\nCOMPUTE RING0 (native NBIO):");
        Probe(h, 0xC900, "COMPUTE_BASE_LO", pat);
        Probe(h, 0xC908, "COMPUTE_CNTL", pat);
        Probe(h, 0xC90C, "COMPUTE_RPTR", pat);
        Probe(h, 0xC918, "COMPUTE_WPTR", pat);

        // Check doorbell/CP registers
        Console.WriteLine("\nAdditional registers:");
        Probe(h, 0xDA70, "RING0_RPTR_ADDR_LO", pat);
        Probe(h, 0xDA74, "RING0_RPTR_ADDR_HI", pat);
        Probe(h, 0xDA7C, "RING0_WPTR_POLL", pat);
        Probe(h, 0xDA80, "RING0_DOORBELL", pat);

        // KIQ registers (Navi10 0xCE00 range)
        Console.WriteLine("\nKIQ ring (GC_BASE shifted):");
        Probe(h, 0xE060, "KIQ_BASE_LO", pat);
        Probe(h, 0xE068, "KIQ_CNTL", pat);
        Probe(h, 0xE06C, "KIQ_RPTR", pat);
        Probe(h, 0xE078, "KIQ_WPTR", pat);

        Console.WriteLine("\n=== Done ===");
        CloseHandle(h);
    }
}

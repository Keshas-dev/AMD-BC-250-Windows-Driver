using System;
using System.Runtime.InteropServices;

class CpGcWriteTest {
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
    static bool WriteReg(IntPtr h, uint off, uint val) {
        byte[] ib = new byte[8];
        BitConverter.GetBytes(off).CopyTo(ib, 0);
        BitConverter.GetBytes(val).CopyTo(ib, 4); uint br;
        return DeviceIoControl(h, WR, ib, 8, null, 0, out br, IntPtr.Zero);
    }
    static void TestWrite(IntPtr h, string name, uint off) {
        uint before = ReadReg(h, off);
        WriteReg(h, off, 0xDEADBEEF);
        uint after = ReadReg(h, off);
        Console.WriteLine("  {0} [{1:X4}]: {2:X8} -> {3:X8} (w={4})", name, off, before, after, before != after ? "YES" : "NO");
        if (before != after) { WriteReg(h, off, before); Console.WriteLine("    restored"); }
    }

    static void Main() {
        IntPtr h = CreateFileW(@"\\.\AMDBC250DreamV43", 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h == (IntPtr)(-1)) { Console.WriteLine("Can't open"); return; }
        byte[] b = new byte[32];
        BitConverter.GetBytes((ulong)0xFE800000).CopyTo(b, 0);
        BitConverter.GetBytes((ulong)0x80000).CopyTo(b, 8);
        BitConverter.GetBytes((ulong)0xC0000000).CopyTo(b, 16);
        BitConverter.GetBytes((ulong)0x40000000).CopyTo(b, 24); uint br;
        DeviceIoControl(h, INIT, b, 32, null, 0, out br, IntPtr.Zero);
        Console.WriteLine("=== CP Write Test via GC_BASE aliased addrs ===\n");

        Console.WriteLine("GC_BASE-shifted NBIO regs (GC_BASE=0x1260):");
        TestWrite(h, "NBIO[0xC100]->0xD360", 0xD360);  // 0x1260+0xC100
        TestWrite(h, "CP_ME_CNTL->0xD2C0", 0xD2C0);    // 0x1260+0xC060

        Console.WriteLine("\nGC_BASE-shifted CP ring regs:");
        TestWrite(h, "RING0_BASE_LO", 0xDA60);
        TestWrite(h, "RING0_BASE_HI", 0xDA64);
        TestWrite(h, "RING0_CNTL", 0xDA68);
        TestWrite(h, "RING0_RPTR", 0xDA6C);
        TestWrite(h, "RING0_WPTR", 0xDA78);

        Console.WriteLine("\nKnown GC regs (for comparison):");
        TestWrite(h, "SPI_MASK", 0x34FC);
        TestWrite(h, "GRBM_SOFT_RESET", 0x326C);

        Console.WriteLine("\n=== Done ===");
        CloseHandle(h);
    }
}

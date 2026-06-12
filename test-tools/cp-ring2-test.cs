using System;
using System.Runtime.InteropServices;

class CpRing2Test {
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

    static void Main() {
        IntPtr h = CreateFileW(@"\\.\AMDBC250DreamV43", 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h == (IntPtr)(-1)) { Console.WriteLine("Can't open"); return; }
        byte[] b = new byte[32];
        BitConverter.GetBytes((ulong)0xFE800000).CopyTo(b, 0);
        BitConverter.GetBytes((ulong)0x80000).CopyTo(b, 8);
        BitConverter.GetBytes((ulong)0xC0000000).CopyTo(b, 16);
        BitConverter.GetBytes((ulong)0x40000000).CopyTo(b, 24); uint br;
        DeviceIoControl(h, INIT, b, 32, null, 0, out br, IntPtr.Zero);
        Console.WriteLine("=== CP Ring GC_BASE Write Test ===\n");

        // ONLY test CP ring regs at GC_BASE offsets — NO NBIO aliases!
        uint[] addrs = { 0xDA60, 0xDA64, 0xDA68, 0xDA6C, 0xDA78 };
        string[] names = { "BASE_LO", "BASE_HI", "CNTL", "RPTR", "WPTR" };

        for (int i = 0; i < addrs.Length; i++) {
            uint a = addrs[i];
            uint before = ReadReg(h, a);
            Console.WriteLine("  RING0_{0} [0x{1:X4}]: before=0x{2:X8}", names[i], a, before);

            // Only write if it's not 0xFFFFFFFF
            if (before != 0xFFFFFFFF) {
                WriteReg(h, a, 0x00000000);
                uint after = ReadReg(h, a);
                Console.WriteLine("    after writing 0: 0x{0:X8} (writable={1})", after, after != before ? "YES" : "NO");
                if (after != before) WriteReg(h, a, before); // restore
            }
        }

        // Also test SPI for comparison
        Console.WriteLine("\n  SPI_MASK [0x34FC]: before=0x{0:X8}", ReadReg(h, 0x34FC));
        WriteReg(h, 0x34FC, 0x00003F00);
        Console.WriteLine("    after=0x{0:X8} (writable=YES)", ReadReg(h, 0x34FC));
        WriteReg(h, 0x34FC, 0x00002000); // restore

        Console.WriteLine("\n=== Done ===");
        CloseHandle(h);
    }
}

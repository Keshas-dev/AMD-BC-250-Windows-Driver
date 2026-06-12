using System;
using System.Runtime.InteropServices;

class CpRingTest {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr CreateFileW(string n, uint a, uint s, IntPtr p, uint c, uint f, IntPtr t);
    [DllImport("kernel32.dll")]
    static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inb, uint inl, byte[] outb, uint outl, out uint br, IntPtr ov);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const uint RR = 0x80000B88, WR = 0x80000B8C, INIT = 0x80000B80;

    static uint ReadReg(IntPtr h, uint off) {
        byte[] ib = new byte[8], ob = new byte[8];
        BitConverter.GetBytes(off).CopyTo(ib, 0);
        uint br;
        if (!DeviceIoControl(h, RR, ib, 8, ob, 8, out br, IntPtr.Zero)) return 0xFFFFFFFF;
        return BitConverter.ToUInt32(ob, 4);
    }
    static bool WriteReg(IntPtr h, uint off, uint val) {
        byte[] ib = new byte[8];
        BitConverter.GetBytes(off).CopyTo(ib, 0);
        BitConverter.GetBytes(val).CopyTo(ib, 4);
        uint br;
        return DeviceIoControl(h, WR, ib, 8, null, 0, out br, IntPtr.Zero);
    }

    static void Main() {
        IntPtr h = CreateFileW(@"\\.\AMDBC250DreamV43", 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h == (IntPtr)(-1)) { Console.WriteLine("Can't open: " + Marshal.GetLastWin32Error()); return; }
        byte[] b = new byte[32];
        BitConverter.GetBytes((ulong)0xFE800000).CopyTo(b, 0);
        BitConverter.GetBytes((ulong)0x80000).CopyTo(b, 8);
        BitConverter.GetBytes((ulong)0xC0000000).CopyTo(b, 16);
        BitConverter.GetBytes((ulong)0x40000000).CopyTo(b, 24);
        uint br;
        DeviceIoControl(h, INIT, b, 32, null, 0, out br, IntPtr.Zero);
        Console.WriteLine("=== CP Ring Register Test ===\n");

        // CP registers at NBIO offsets
        Console.WriteLine("CP control:");
        Console.WriteLine("  CP_ME_CNTL [0xC060] = 0x{0:X8}", ReadReg(h, 0xC060));
        Console.WriteLine("  CP_ME_STATUS [0xC064] = 0x{0:X8}", ReadReg(h, 0xC064));
        Console.WriteLine("  CP_MEC_CNTL [0xC0E0] = 0x{0:X8}", ReadReg(h, 0xC0E0));
        Console.WriteLine("  CP_MEC_STATUS [0xC0E4] = 0x{0:X8}", ReadReg(h, 0xC0E4));

        // GFX ring registers
        Console.WriteLine("\nGFX ring:");
        Console.WriteLine("  RING0_BASE_LO [0xC800] = 0x{0:X8}", ReadReg(h, 0xC800));
        Console.WriteLine("  RING0_BASE_HI [0xC804] = 0x{0:X8}", ReadReg(h, 0xC804));
        Console.WriteLine("  RING0_CNTL [0xC808] = 0x{0:X8}", ReadReg(h, 0xC808));
        Console.WriteLine("  RING0_RPTR [0xC80C] = 0x{0:X8}", ReadReg(h, 0xC80C));
        Console.WriteLine("  RING0_WPTR [0xC818] = 0x{0:X8}", ReadReg(h, 0xC818));

        // Write test: halt CP, read back
        Console.WriteLine("\nCP write test (halt CP):");
        Console.WriteLine("  Writing 0x00010003 to CP_ME_CNTL [0xC060]...");
        uint meBefore = ReadReg(h, 0xC060);
        WriteReg(h, 0xC060, 0x00010003);
        uint meAfter = ReadReg(h, 0xC060);
        Console.WriteLine("  Before=0x{0:X8} After=0x{1:X8} (diff={2})", meBefore, meAfter, meBefore != meAfter ? "YES" : "NO");
        if (meBefore != meAfter) { WriteReg(h, 0xC060, meBefore); Console.WriteLine("  Restored"); }

        // GRBM test
        Console.WriteLine("\nGRBM:");
        Console.WriteLine("  GRBM_STATUS [0x3260] = 0x{0:X8}", ReadReg(h, 0x3260));
        Console.WriteLine("  GRBM_SOFT_RESET [0x326C] = 0x{0:X8}", ReadReg(h, 0x326C));

        // Scratch
        Console.WriteLine("\nScratch:");
        Console.WriteLine("  SCRATCH [0x8500] = 0x{0:X8}", ReadReg(h, 0x8500));
        Console.WriteLine("  SCRATCH [0x8504] = 0x{0:X8}", ReadReg(h, 0x8504));

        Console.WriteLine("\n=== Done ===");
        CloseHandle(h);
    }
}

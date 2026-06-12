using System;
using System.Runtime.InteropServices;

class CpScan {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr CreateFileW(string n, uint a, uint s, IntPtr p, uint c, uint f, IntPtr t);
    [DllImport("kernel32.dll")]
    static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inb, uint inl, byte[] outb, uint outl, out uint br, IntPtr ov);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const uint RR = 0x80000B88, INIT = 0x80000B80;

    static uint ReadReg(IntPtr h, uint off) {
        byte[] ib = new byte[8], ob = new byte[8];
        BitConverter.GetBytes(off).CopyTo(ib, 0);
        uint br;
        if (!DeviceIoControl(h, RR, ib, 8, ob, 8, out br, IntPtr.Zero)) return 0xFFFFFFFF;
        return BitConverter.ToUInt32(ob, 4);
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
        Console.WriteLine("=== CP Register Probe ===\n");

        // Old Navi10 CP area (0x2800-0x28FF)
        Console.WriteLine("Old Navi10 CP area (BLOCKED=0xFFFFFFFF, shifted=maybe live):");
        Console.WriteLine("  Old[0x2800] = 0x{0:X8} -> shifted 0x3A60 = 0x{1:X8}", ReadReg(h, 0x2800), ReadReg(h, 0x3A60));
        Console.WriteLine("  Old[0x2810] = 0x{0:X8} -> shifted 0x3A70 = 0x{1:X8}", ReadReg(h, 0x2810), ReadReg(h, 0x3A70));
        Console.WriteLine("  Old[0x2820] = 0x{0:X8} -> shifted 0x3A80 = 0x{1:X8}", ReadReg(h, 0x2820), ReadReg(h, 0x3A80));
        Console.WriteLine("  Old[0x2880] = 0x{0:X8} -> shifted 0x3AE0 = 0x{1:X8}", ReadReg(h, 0x2880), ReadReg(h, 0x3AE0));
        Console.WriteLine("  Old[0x2890] = 0x{0:X8} -> shifted 0x3AF0 = 0x{1:X8}", ReadReg(h, 0x2890), ReadReg(h, 0x3AF0));
        Console.WriteLine("  Old[0x28A0] = 0x{0:X8} -> shifted 0x3B00 = 0x{1:X8}", ReadReg(h, 0x28A0), ReadReg(h, 0x3B00));
        Console.WriteLine("  Old[0x28B0] = 0x{0:X8} -> shifted 0x3B10 = 0x{1:X8}", ReadReg(h, 0x28B0), ReadReg(h, 0x3B10));

        // Also check segment 1 (0xA000) for CP
        Console.WriteLine("\nSegment 1 base (0xA000):");
        Console.WriteLine("  SEG1[0xA280] = 0x{0:X8}", ReadReg(h, 0xA280));
        Console.WriteLine("  SEG1[0xA290] = 0x{0:X8}", ReadReg(h, 0xA290));
        Console.WriteLine("  SEG1[0xA2A0] = 0x{0:X8}", ReadReg(h, 0xA2A0));

        // Check what GRBM_SOFT_RESET says
        Console.WriteLine("\nKey registers:");
        Console.WriteLine("  GRBM_STATUS [0x3260] = 0x{0:X8}", ReadReg(h, 0x3260));
        Console.WriteLine("  GRBM_SOFT_RESET [0x326C] = 0x{0:X8}", ReadReg(h, 0x326C));
        Console.WriteLine("  Scratch [0x32D4] = 0x{0:X8}", ReadReg(h, 0x32D4));

        CloseHandle(h);
    }
}

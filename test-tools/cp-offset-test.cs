using System;
using System.Runtime.InteropServices;

class CpOffsetTest {
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
        Console.WriteLine("=== CP Register Offset Probe ===\n");

        // Test CP ring registers at multiple possible offsets
        uint[] testOffsets = {
            0xC800, 0xC804, 0xC808, 0xC80C, 0xC818, // NBIO offsets
            0x3260, 0x3264, 0x34FC,                   // Known GC offsets
            0x3A60, 0x3A68, 0x3A70, 0x3A80,           // GC_BASE+0x2800 area
            0x3C00, 0x3C04, 0x3C08, 0x3C0C, 0x3C18,  // Try GC_BASE+0x2A00 area
            0x3E00, 0x3E04, 0x3E08, 0x3E0C, 0x3E18,  // Try GC_BASE+0x2C00 area
            0x4000, 0x4004, 0x4008, 0x400C, 0x4018,  // Try GC_BASE+0x2E00 area
            0x4200, 0x4204, 0x4208, 0x420C, 0x4218,  // Try GC_BASE+0x3000 area
            0x4400, 0x4404, 0x4408, 0x440C, 0x4418,  // Try GC_BASE+0x3200 area
        };

        foreach (uint off in testOffsets) {
            uint val = ReadReg(h, off);
            if (val != 0xFFFFFFFF)
                Console.WriteLine("  [0x{0:X4}] = 0x{1:X8}", off, val);
        }

        // Known GC_BASE shifted CP registers
        Console.WriteLine("\nGC_BASE applied to CP ring regs (0xC800->0xDA60):");
        Console.WriteLine("  RING0_BASE_LO [0xDA60] = 0x{0:X8}", ReadReg(h, 0xDA60));
        Console.WriteLine("  RING0_BASE_HI [0xDA64] = 0x{0:X8}", ReadReg(h, 0xDA64));
        Console.WriteLine("  RING0_CNTL [0xDA68] = 0x{0:X8}", ReadReg(h, 0xDA68));
        Console.WriteLine("  RING0_RPTR [0xDA6C] = 0x{0:X8}", ReadReg(h, 0xDA6C));
        Console.WriteLine("  RING0_WPTR [0xDA78] = 0x{0:X8}", ReadReg(h, 0xDA78));

        // Also check CP_ME_CNTL at GC_BASE shifted (0xC060->0xD2C0)
        Console.WriteLine("\nCP_ME_CNTL at GC_BASE shifted:");
        Console.WriteLine("  [0xD2C0] = 0x{0:X8}", ReadReg(h, 0xD2C0));

        // Check segment 1 (0xA000) for CP regs too
        Console.WriteLine("\nSegment 1 (base 0xA000) CP area:");
        Console.WriteLine("  RING0_BASE_LO [0xB800] = 0x{0:X8}", ReadReg(h, 0xB800));
        Console.WriteLine("  RING0_CNTL [0xB808] = 0x{0:X8}", ReadReg(h, 0xB808));

        CloseHandle(h);
    }
}

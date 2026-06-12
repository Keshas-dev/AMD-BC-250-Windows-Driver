using System;
using System.Runtime.InteropServices;

class SpiWriteTest {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint create, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inb, uint inl, byte[] outb, uint outl, out uint br, IntPtr ov);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const uint IOCTL_READ_REG  = 0x80000B88;
    const uint IOCTL_WRITE_REG = 0x80000B8C;
    const uint IOCTL_INIT_HW   = 0x80000B80;

    static IntPtr h;

    static uint ReadReg(uint off) {
        byte[] inb = new byte[8], outb = new byte[8];
        BitConverter.GetBytes(off).CopyTo(inb, 0);
        uint br;
        if (!DeviceIoControl(h, IOCTL_READ_REG, inb, 8, outb, 8, out br, IntPtr.Zero)) return 0xFFFFFFFF;
        return BitConverter.ToUInt32(outb, 4);
    }

    static bool WriteReg(uint off, uint val) {
        byte[] inb = new byte[8];
        BitConverter.GetBytes(off).CopyTo(inb, 0);
        BitConverter.GetBytes(val).CopyTo(inb, 4);
        uint br;
        return DeviceIoControl(h, IOCTL_WRITE_REG, inb, 8, null, 0, out br, IntPtr.Zero);
    }

    static void Main() {
        h = CreateFileW(@"\\.\AMDBC250DreamV43", 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h == (IntPtr)(-1)) {
            Console.WriteLine("Cannot open device: " + Marshal.GetLastWin32Error());
            return;
        }
        byte[] initBuf = new byte[32];
        BitConverter.GetBytes((ulong)0xFE800000).CopyTo(initBuf, 0);
        BitConverter.GetBytes((ulong)0x80000).CopyTo(initBuf, 8);
        BitConverter.GetBytes((ulong)0xC0000000).CopyTo(initBuf, 16);
        BitConverter.GetBytes((ulong)0x40000000).CopyTo(initBuf, 24);
        uint br;
        DeviceIoControl(h, IOCTL_INIT_HW, initBuf, 32, null, 0, out br, IntPtr.Zero);
        Console.WriteLine("=== SPI WGP Write Test ===\n");

        // GC register checks
        Console.WriteLine("GC status:");
        Console.WriteLine("  GRBM_STATUS [0x3260] = 0x{0:X8}", ReadReg(0x3260));
        Console.WriteLine("  CC_GC_SHADER_ARRAY_CONFIG [0x3264] = 0x{0:X8}", ReadReg(0x3264));

        // SPI_PG_ENABLE_STATIC_WGP_MASK write test
        Console.WriteLine("\nSPI_PG_ENABLE_STATIC_WGP_MASK [0x34FC]:");
        uint spiBefore = ReadReg(0x34FC);
        Console.WriteLine("  Before: 0x{0:X8}  (WGP mask: bits 8-13)", spiBefore);

        // Try enabling all 6 WGPs (bits 8-13 = 0x3F00)
        Console.WriteLine("\n  Writing 0x00003F00 (enable WGP0-5)...");
        bool ok = WriteReg(0x34FC, 0x00003F00);
        uint spiAfter = ReadReg(0x34FC);
        Console.WriteLine("  After:  0x{0:X8}  (write={1})", spiAfter, ok);

        if (spiAfter != spiBefore) {
            Console.WriteLine("\n  *** SPI WRITE WORKS! WGP mask changed! ***");
            Console.WriteLine("  Restoring original 0x{0:X8}...", spiBefore);
            WriteReg(0x34FC, spiBefore);
            uint spiRestore = ReadReg(0x34FC);
            Console.WriteLine("  Restored: 0x{0:X8}", spiRestore);
            if (spiRestore == spiBefore)
                Console.WriteLine("  *** Restore OK ***");
        } else {
            Console.WriteLine("\n  Write ignored (GC power-gated or register read-only)");
        }

        // Also test CC_GC_SHADER_ARRAY_CONFIG
        Console.WriteLine("\nCC_GC_SHADER_ARRAY_CONFIG [0x3264]:");
        uint ccBefore = ReadReg(0x3264);
        Console.WriteLine("  Before: 0x{0:X8}", ccBefore);
        Console.WriteLine("  Writing 0xFFE00000 (40 CU mask) ...");
        WriteReg(0x3264, 0xFFE00000);
        uint ccAfter = ReadReg(0x3264);
        Console.WriteLine("  After:  0x{0:X8}", ccAfter);
        if (ccAfter == ccBefore)
            Console.WriteLine("  Write ignored (fused read-only)");
        else
            Console.WriteLine("  *** CC WRITE WORKS! ***");
        WriteReg(0x3264, ccBefore); // restore

        Console.WriteLine("\n=== Complete ===");
        CloseHandle(h);
    }
}

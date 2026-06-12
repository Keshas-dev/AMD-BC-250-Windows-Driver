using System;
using System.Runtime.InteropServices;

class SmuPointTest {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint create, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inb, uint inl, byte[] outb, uint outl, out uint br, IntPtr ov);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const uint IOCTL_READ_REG  = 0x80000B88;
    const uint IOCTL_INIT_HW   = 0x80000B80;

    static uint ReadReg(IntPtr h, uint off) {
        byte[] inb = new byte[8], outb = new byte[8];
        BitConverter.GetBytes(off).CopyTo(inb, 0);
        uint br;
        if (!DeviceIoControl(h, IOCTL_READ_REG, inb, 8, outb, 8, out br, IntPtr.Zero)) return 0xFFFFFFFF;
        return BitConverter.ToUInt32(outb, 4);
    }

    static void Main() {
        IntPtr h = CreateFileW(@"\\.\AMDBC250DreamV43", 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h == (IntPtr)(-1)) { Console.WriteLine("Cannot open device: " + Marshal.GetLastWin32Error()); return; }
        byte[] initBuf = new byte[32];
        BitConverter.GetBytes((ulong)0xFE800000).CopyTo(initBuf, 0);
        BitConverter.GetBytes((ulong)0x80000).CopyTo(initBuf, 8);
        BitConverter.GetBytes((ulong)0xC0000000).CopyTo(initBuf, 16);
        BitConverter.GetBytes((ulong)0x40000000).CopyTo(initBuf, 24);
        uint br;
        DeviceIoControl(h, IOCTL_INIT_HW, initBuf, 32, null, 0, out br, IntPtr.Zero);
        Console.WriteLine("=== SMU Point Probe ===\n");

        // Read exactly at MP1_BASE (0x16000) to see what's there
        uint v16000 = ReadReg(h, 0x16000);
        Console.WriteLine("BAR5+0x16000 = 0x{0:X8} (claimed MP1_BASE)", v16000);

        // Read a few key MP1 base signature registers at each possible base
        uint[] bases = { 0x00000, 0x04000, 0x08000, 0x0C000, 0x10000, 0x14000, 0x16000, 0x18000, 0x1C000 };
        foreach (uint b in bases) {
            uint val = ReadReg(h, b);
            if (val != 0 && val != 0xFFFFFFFF)
                Console.WriteLine("base=0x{0:X5}: [0x{0:X5}] = 0x{1:X8}", b, val);
        }

        // Also check if THM_CTRL really is at 0x8000: read 0x8004
        Console.WriteLine("\nTHM area:");
        Console.WriteLine("  [0x8000] = 0x{0:X8}", ReadReg(h, 0x8000));
        Console.WriteLine("  [0x8004] = 0x{0:X8}", ReadReg(h, 0x8004));
        Console.WriteLine("  [0x8008] = 0x{0:X8} (temp)", ReadReg(h, 0x8008));
        Console.WriteLine("  [0x800C] = 0x{0:X8}", ReadReg(h, 0x800C));

        // Check GC registers still work
        Console.WriteLine("\nGC check:");
        Console.WriteLine("  GRBM_STATUS [0x3260] = 0x{0:X8}", ReadReg(h, 0x3260));
        Console.WriteLine("  Scratch [0x32D4] = 0x{0:X8}", ReadReg(h, 0x32D4));

        Console.WriteLine("\n=== Done (no probe, only known-safe regs) ===");
        CloseHandle(h);
    }
}

using System;
using System.Runtime.InteropServices;

class NbioUnlockTest {
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
        Console.WriteLine("=== NBIO Unlock Test ===\n");

        // Read current NBIO signature registers
        uint sig1 = ReadReg(h, 0xC100);
        uint sig2 = ReadReg(h, 0xC180);
        Console.WriteLine("NBIO signature registers:");
        Console.WriteLine("  NBIO[0xC100] = 0x{0:X8}", sig1);
        Console.WriteLine("  NBIO[0xC180] = 0x{0:X8}", sig2);

        // Read MMHUB before
        uint mmhubBefore = ReadReg(h, 0x50D0);
        Console.WriteLine("  MMHUB[0x50D0] = 0x{0:X8}", mmhubBefore);

        // Try writing NBIO signature values through GPU driver
        Console.WriteLine("\nWriting NBIO signature values via GPU driver WRITE_REG:");
        Console.WriteLine("  Writing 0xFEDCBAEF to NBIO[0xC100]...");
        WriteReg(h, 0xC100, 0xFEDCBAEF);
        Console.WriteLine("  Writing 0xFEDCBADF to NBIO[0xC180]...");
        WriteReg(h, 0xC180, 0xFEDCBADF);

        // Read back
        uint sig1After = ReadReg(h, 0xC100);
        uint sig2After = ReadReg(h, 0xC180);
        uint mmhubAfter = ReadReg(h, 0x50D0);
        Console.WriteLine("\nAfter unlock attempt:");
        Console.WriteLine("  NBIO[0xC100] = 0x{0:X8} (changed={1})", sig1After, sig1After != sig1 ? "YES" : "NO");
        Console.WriteLine("  NBIO[0xC180] = 0x{0:X8} (changed={1})", sig2After, sig2After != sig2 ? "YES" : "NO");
        Console.WriteLine("  MMHUB[0x50D0] = 0x{0:X8} (changed={1})", mmhubAfter, mmhubAfter != mmhubBefore ? "YES" : "NO");

        // Also check if CP_ME_CNTL became writable after unlock
        if (sig1After != sig1 || sig2After != sig2) {
            Console.WriteLine("\nNBIO unlock APPEARS to have worked! Testing CP write:");
            uint cpBefore = ReadReg(h, 0xC060);
            WriteReg(h, 0xC060, 0x00010003);
            uint cpAfter = ReadReg(h, 0xC060);
            Console.WriteLine("  CP_ME_CNTL: {0:X8} -> {0:X8} (writable={1})", cpBefore, cpAfter, cpBefore != cpAfter ? "YES" : "NO");
            if (cpBefore != cpAfter) WriteReg(h, 0xC060, cpBefore);
        }

        Console.WriteLine("\n=== Done ===");
        CloseHandle(h);
    }
}

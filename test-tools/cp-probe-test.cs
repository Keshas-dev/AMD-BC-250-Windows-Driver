using System;
using System.Runtime.InteropServices;

class CpProbeTest {
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
        if (h == (IntPtr)(-1)) { Console.WriteLine("Open: " + Marshal.GetLastWin32Error()); return; }
        byte[] b = new byte[32];
        BitConverter.GetBytes((ulong)0xFE800000).CopyTo(b, 0);
        BitConverter.GetBytes((ulong)0x80000).CopyTo(b, 8);
        BitConverter.GetBytes((ulong)0xC0000000).CopyTo(b, 16);
        BitConverter.GetBytes((ulong)0x40000000).CopyTo(b, 24); uint br;
        DeviceIoControl(h, INIT, b, 32, null, 0, out br, IntPtr.Zero);
        Console.WriteLine("=== CP Register Write-Back Probe ===\n");

        uint[] testOffsets = {
            0xDA60, 0xDA64, 0xDA68, 0xDA6C, 0xDA78, // GC_BASE-shifted CP ring
            0xC800, 0xC804, 0xC808, 0xC80C, 0xC818, // Native NBIO CP ring
            0xD2C0, 0xD2C4, 0xD2E0, 0xD2E4, // GC_BASE-shifted ME/MEC
            0x3260, 0x326C, 0x34FC, 0x32D4, // GC regs (known good)
            0x3AE0, 0x3AF0,               // Alive GC_BASE registers
            0xC060, 0xC0E0                // Native ME/MEC
        };
        string[] names = {
            "SHIFT_BASE_LO","SHIFT_BASE_HI","SHIFT_CNTL","SHIFT_RPTR","SHIFT_WPTR",
            "NATIVE_BASE_LO","NATIVE_BASE_HI","NATIVE_CNTL","NATIVE_RPTR","NATIVE_WPTR",
            "SHIFT_ME_CNTL","SHIFT_ME_STATUS","SHIFT_MEC_CNTL","SHIFT_MEC_STATUS",
            "GRBM_STATUS","GRBM_SOFT_RESET","SPI_MASK","SCRATCH",
            "REG_3AE0","REG_3AF0",
            "NATIVE_ME_CNTL","NATIVE_MEC_CNTL"
        };

        uint testPattern = 0xDEADBEEF;

        for (int i = 0; i < testOffsets.Length; i++) {
            uint a = testOffsets[i];
            uint before = ReadReg(h, a);
            Console.Write("{0,-20} [0x{1:X6}] before=0x{2:X8}", names[i], a, before);

            if (before != 0xFFFFFFFF) {
                // Try writing a non-zero pattern
                WriteReg(h, a, testPattern);
                uint after = ReadReg(h, a);
                bool writable = (after == testPattern);

                // Restore
                if (writable && before != after) WriteReg(h, a, before);
                string result = writable ? "WRITABLE" : "readonly";
                Console.Write(" -> write={0}: 0x{1:X8}  [{2}]", testPattern, after, result);
            } else {
                Console.Write(" -> UNMAPPED");
            }
            Console.WriteLine();
        }

        Console.WriteLine("\n=== Done ===");
        CloseHandle(h);
    }
}

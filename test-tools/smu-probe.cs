using System;
using System.Runtime.InteropServices;

class SmuProbe {
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
        Console.WriteLine("=== SMU v11.8 BAR5 Area Probe ===\n");

        Console.WriteLine("Probing 0x16000 - 0x17000 (every 0x10):");
        uint off;
        for (off = 0x16000; off < 0x17000; off += 0x10) {
            uint v = ReadReg(h, off);
            if (v != 0 && v != 0xFFFFFFFF)
                Console.WriteLine("  BAR5+0x{0:X5} = 0x{1:X8}", off, v);
        }

        Console.WriteLine("\nMP1 C2PMSG registers at 0x16000 base:");
        Console.WriteLine("  C2PMSG_33 (0x16284) = 0x{0:X8}", ReadReg(h, 0x16284));
        Console.WriteLine("  C2PMSG_35 (0x1628C) = 0x{0:X8}", ReadReg(h, 0x1628C));
        Console.WriteLine("  C2PMSG_64 (0x16100) = 0x{0:X8}", ReadReg(h, 0x16100));
        Console.WriteLine("  C2PMSG_65 (0x16104) = 0x{0:X8}", ReadReg(h, 0x16104));
        Console.WriteLine("  C2PMSG_81 (0x16A04) = 0x{0:X8}", ReadReg(h, 0x16A04));
        Console.WriteLine("  C2PMSG_92 (0x16A70) = 0x{0:X8}", ReadReg(h, 0x16A70));

        Console.WriteLine("\nOld SMU v11.0 area (0x16200+):");
        Console.WriteLine("  C2PMSG_66 (0x16208) = 0x{0:X8}", ReadReg(h, 0x16208));
        Console.WriteLine("  C2PMSG_82 (0x16248) = 0x{0:X8}", ReadReg(h, 0x16248));
        Console.WriteLine("  C2PMSG_90 (0x16268) = 0x{0:X8}", ReadReg(h, 0x16268));

        Console.WriteLine("\nProbing MP1 base candidates (every 0x1000):");
        uint baddr;
        for (baddr = 0x00000; baddr < 0x80000; baddr += 0x1000) {
            uint c2pmsg81 = ReadReg(h, baddr + 0x244);
            uint c2pmsg64 = ReadReg(h, baddr + 0x100);
            if (c2pmsg81 != 0 && c2pmsg81 != 0xFFFFFFFF)
                Console.WriteLine("  base=0x{0:X5}: C2PMSG_81=0x{1:X8} C2PMSG_64=0x{2:X8}", baddr, c2pmsg81, c2pmsg64);
        }

        CloseHandle(h);
    }
}

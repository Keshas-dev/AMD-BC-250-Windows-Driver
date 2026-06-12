using System;
using System.Runtime.InteropServices;

class SmuWriteTest {
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

    static void TestReg(string name, uint off) {
        uint before = ReadReg(off);
        Console.WriteLine("  {0} [{1:X5}] before = 0x{2:X8}", name, off, before);
        bool ok = WriteReg(off, 0xDEADBEEF);
        uint after = ReadReg(off);
        Console.WriteLine("  {0} [{1:X5}] after  = 0x{2:X8} (write={3})", name, off, after, ok);
        WriteReg(off, before); // restore
        if (before != after) Console.WriteLine("  --> WRITABLE!");
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
        Console.WriteLine("=== SMU Write-Back Test ===\n");

        // Test specific SMU C2PMSG registers at corrected offsets
        Console.WriteLine("1. Corrected BC-250 SMU registers:");
        TestReg("C2PMSG_66", 0x16A08);
        TestReg("C2PMSG_82", 0x16A48);
        TestReg("C2PMSG_83", 0x16A4C);
        TestReg("C2PMSG_90", 0x16A68);

        // Test old Navi10 offsets to compare
        Console.WriteLine("\n2. Old Navi10 SMU offsets:");
        TestReg("C2PMSG_66(OLD)", 0x16104);
        TestReg("C2PMSG_82(OLD)", 0x16148);
        TestReg("C2PMSG_90(OLD)", 0x16168);

        // Test THM at both offsets
        Console.WriteLine("\n3. THM thermal registers:");
        TestReg("THM_CTRL(OLD)", 0x8000);
        TestReg("THM_CURR(OLD)", 0x8008);
        TestReg("THM_CTRL(NEW)", 0x1662C);
        TestReg("THM_CURR(NEW)", 0x1677C);

        Console.WriteLine("\n=== Complete ===");
        CloseHandle(h);
    }
}

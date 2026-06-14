using System;
using System.Runtime.InteropServices;

class KiqTest
{
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr sec, uint creation, uint flags, IntPtr templ);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inbuf, uint inlen, byte[] outbuf, uint outlen, out uint retlen, IntPtr ovl);

    static void Main()
    {
        Console.WriteLine("KIQ Test starting...");
        IntPtr h = CreateFile(@"\\.\AMDBC250DreamV43", 0xC0000000, 3, IntPtr.Zero, 3, 0x80, IntPtr.Zero);
        if (h == (IntPtr)(-1))
        {
            Console.WriteLine("FAIL: Can't open device");
            return;
        }
        Console.WriteLine("Device opened OK");

        uint READ_REG = 0x80000B88;
        uint retlen;

        byte[] inbuf = BitConverter.GetBytes((uint)0xE060);
        byte[] outbuf = new byte[4];
        if (DeviceIoControl(h, READ_REG, inbuf, 4, outbuf, 4, out retlen, IntPtr.Zero))
        {
            uint val = BitConverter.ToUInt32(outbuf, 0);
            Console.WriteLine("KIQ_BASE_LO [0x000E060] = 0x" + val.ToString("X8"));
        }

        inbuf = BitConverter.GetBytes((uint)0xE078);
        if (DeviceIoControl(h, READ_REG, inbuf, 4, outbuf, 4, out retlen, IntPtr.Zero))
        {
            uint val = BitConverter.ToUInt32(outbuf, 0);
            Console.WriteLine("KIQ_WPTR   [0x000E078] = 0x" + val.ToString("X8"));
        }

        inbuf = BitConverter.GetBytes((uint)0xE068);
        if (DeviceIoControl(h, READ_REG, inbuf, 4, outbuf, 4, out retlen, IntPtr.Zero))
        {
            uint val = BitConverter.ToUInt32(outbuf, 0);
            Console.WriteLine("KIQ_CNTL   [0x000E068] = 0x" + val.ToString("X8"));
        }

        inbuf = BitConverter.GetBytes((uint)0xDA68);
        if (DeviceIoControl(h, READ_REG, inbuf, 4, outbuf, 4, out retlen, IntPtr.Zero))
        {
            uint val = BitConverter.ToUInt32(outbuf, 0);
            Console.WriteLine("RING_CNTL  [0x000DA68] = 0x" + val.ToString("X8"));
        }

        Console.WriteLine("Done");
    }
}

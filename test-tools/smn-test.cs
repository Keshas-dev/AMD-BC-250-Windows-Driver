using System;
using System.Runtime.InteropServices;

class SmnTest {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint create, uint flags, IntPtr tmpl);
    
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inb, uint inl, byte[] outb, uint outl, out uint br, IntPtr ov);
    
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    static IntPtr Open() {
        return CreateFileW(@"\\.\AMDBC250DreamV43", 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
    }

    static uint ReadReg(IntPtr h, uint off) {
        byte[] inb = new byte[8];
        byte[] outb = new byte[8];
        BitConverter.GetBytes(off).CopyTo(inb, 0);
        uint br;
        if (!DeviceIoControl(h, 0x80000B88, inb, 8, outb, 8, out br, IntPtr.Zero))
            return 0xFFFFFFFF;
        return BitConverter.ToUInt32(outb, 4);
    }

    static bool WriteReg(IntPtr h, uint off, uint val) {
        byte[] inb = new byte[8];
        BitConverter.GetBytes(off).CopyTo(inb, 0);
        BitConverter.GetBytes(val).CopyTo(inb, 4);
        uint br;
        return DeviceIoControl(h, 0x80000B8C, inb, 8, null, 0, out br, IntPtr.Zero);
    }

    static void Main() {
        IntPtr h = Open();
        if (h == (IntPtr)(-1)) {
            Console.WriteLine("Cannot open device: " + Marshal.GetLastWin32Error());
            return;
        }

        Console.WriteLine("=== NBIO SMN Interface Probe ===\n");
        Console.WriteLine("Step 1: NBIO baseline (0xC100-0xC1FC):\n");
        for (uint off = 0xC100; off <= 0xC1FC; off += 4) {
            uint val = ReadReg(h, off);
            Console.WriteLine("  0x{0:X4} = 0x{1:X8}", off, val);
        }

        Console.WriteLine("\nStep 2: Test candidate SMN index/data pairs\n");
        var pairs = new Tuple<uint,uint,string>[] {
            Tuple.Create(0xC100u, 0xC104u, "NBIO base +0x00/+0x04"),
            Tuple.Create(0xC120u, 0xC124u, "NBIO base +0x20/+0x24"),
            Tuple.Create(0xC140u, 0xC144u, "NBIO base +0x40/+0x44"),
            Tuple.Create(0xC160u, 0xC164u, "NBIO base +0x60/+0x64"),
            Tuple.Create(0xC180u, 0xC184u, "NBIO base +0x80/+0x84"),
            Tuple.Create(0xC1A0u, 0xC1A4u, "NBIO base +0xA0/+0xA4"),
            Tuple.Create(0xC1C0u, 0xC1C4u, "NBIO base +0xC0/+0xC4"),
            Tuple.Create(0xC1E0u, 0xC1E4u, "NBIO base +0xE0/+0xE4"),
        };

        uint[] testAddrs = { 0x00008000, 0x00000000, 0x0000BEEF, 0x00020004, 0x00016104, 0x00016284 };

        foreach (var p in pairs) {
            uint idx = p.Item1, dataOff = p.Item2;
            Console.WriteLine("  {0}: idx=0x{1:X4} data=0x{2:X4}", p.Item3, idx, dataOff);
            foreach (uint ta in testAddrs) {
                WriteReg(h, idx, ta);
                System.Threading.Thread.Sleep(1);
                uint v = ReadReg(h, dataOff);
                Console.WriteLine("    idx(0x{0:X8})->data=0x{1:X8}", ta, v);
            }
            Console.WriteLine();
        }

        // Step 3: Check if any register acts as SMN - write idx, read back idx to see if it latches
        Console.WriteLine("Step 3: Register latching test\n");
        foreach (var p in pairs) {
            uint idx = p.Item1;
            uint orig = ReadReg(h, idx);
            WriteReg(h, idx, 0x12345678);
            uint after = ReadReg(h, idx);
            WriteReg(h, idx, orig);
            Console.WriteLine("  0x{0:X4}: {1:X8} -> 0x12345678 -> {2:X8} (writes {3})", 
                idx, orig, after, (orig != after && after == 0x12345678) ? "PERSIST" : "DISCARDED/BLOCKED");
        }

        Console.WriteLine("\nStep 4: Direct 0xC160 write 0xDEAD, read 0xC164\n");
        WriteReg(h, 0xC160, 0x0000DEAD);
        uint d1 = ReadReg(h, 0xC164);
        WriteReg(h, 0xC160, 0x0000BEEF);
        uint d2 = ReadReg(h, 0xC164);
        Console.WriteLine("  idx(0xDEAD)->data=0x{0:X8}", d1);
        Console.WriteLine("  idx(0xBEEF)->data=0x{0:X8}", d2);
        if (d1 != d2) Console.WriteLine("  *** SMN-like behavior detected! ***");

        CloseHandle(h);
    }
}

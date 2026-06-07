using System;
using System.Runtime.InteropServices;

class PciIndirectTest {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint create, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inb, uint inl, byte[] outb, uint outl, out uint br, IntPtr ov);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    static IntPtr Open() {
        return CreateFileW(@"\\.\AMDBC250DreamV43", 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
    }

    // ReadReg/WriteReg via BAR5 MMIO
    static uint ReadReg(IntPtr h, uint off) {
        byte[] inb = new byte[8], outb = new byte[8];
        BitConverter.GetBytes(off).CopyTo(inb, 0);
        uint br;
        if (!DeviceIoControl(h, 0x80000B88, inb, 8, outb, 8, out br, IntPtr.Zero)) return 0xFFFFFFFF;
        return BitConverter.ToUInt32(outb, 4);
    }
    static bool WriteReg(IntPtr h, uint off, uint val) {
        byte[] inb = new byte[8];
        BitConverter.GetBytes(off).CopyTo(inb, 0);
        BitConverter.GetBytes(val).CopyTo(inb, 4);
        uint br;
        return DeviceIoControl(h, 0x80000B8C, inb, 8, null, 0, out br, IntPtr.Zero);
    }

    // READ_PCI_CONFIG (0x80000BAC): reads full 256-byte config
    // Input: Bus(4), Device(4), Function(4) = 12 bytes
    // Output: Bus(4), Device(4), Function(4), BytesRead(4), ConfigData[256] = 268 bytes
    static byte[] ReadPciConfig(IntPtr h, uint bus, uint dev, uint func) {
        byte[] inb = new byte[12];
        BitConverter.GetBytes(bus).CopyTo(inb, 0);
        BitConverter.GetBytes(dev).CopyTo(inb, 4);
        BitConverter.GetBytes(func).CopyTo(inb, 8);
        byte[] outb = new byte[268];
        uint br;
        if (!DeviceIoControl(h, 0x80000BAC, inb, 12, outb, 268, out br, IntPtr.Zero)) return null;
        return outb;
    }

    // WRITE_PCI_CONFIG (0x80000BB0): write one DWORD
    // Input: Bus(4), Device(4), Function(4), Offset(4), Value(4) = 20 bytes
    static bool WritePciConfig(IntPtr h, uint bus, uint dev, uint func, uint off, uint val) {
        byte[] inb = new byte[20];
        BitConverter.GetBytes(bus).CopyTo(inb, 0);
        BitConverter.GetBytes(dev).CopyTo(inb, 4);
        BitConverter.GetBytes(func).CopyTo(inb, 8);
        BitConverter.GetBytes(off).CopyTo(inb, 12);
        BitConverter.GetBytes(val).CopyTo(inb, 16);
        uint br;
        return DeviceIoControl(h, 0x80000BB0, inb, 20, null, 0, out br, IntPtr.Zero);
    }

    static uint GetConfigDword(byte[] cfg, uint off) {
        if (cfg == null || off + 4 > 256) return 0xFFFFFFFF;
        return BitConverter.ToUInt32(cfg, (int)(16 + off)); // ConfigData starts at offset 16
    }

    static void Main() {
        IntPtr h = Open();
        if (h == (IntPtr)(-1)) {
            Console.WriteLine("Cannot open device: " + Marshal.GetLastWin32Error());
            return;
        }

        Console.WriteLine("=== PCI Config Indirect Register Access Test ===\n");

        // Init hardware
        Console.WriteLine("Step 0: Init hardware...");
        byte[] initBuf = new byte[32];
        BitConverter.GetBytes((ulong)0xFE800000).CopyTo(initBuf, 0);
        BitConverter.GetBytes((ulong)0x80000).CopyTo(initBuf, 8);
        BitConverter.GetBytes((ulong)0xC0000000).CopyTo(initBuf, 16);
        BitConverter.GetBytes((ulong)0x40000000).CopyTo(initBuf, 24);
        uint br;
        DeviceIoControl(h, 0x80000B80, initBuf, 32, null, 0, out br, IntPtr.Zero);
        Console.WriteLine("  Done.\n");

        // GPU: bus=1, dev=0, func=0
        uint bus=1, dev=0, func=0;

        Console.WriteLine("Step 1: Read GPU PCI config space (full 256 bytes):\n");
        byte[] cfg = ReadPciConfig(h, bus, dev, func);
        if (cfg == null) {
            Console.WriteLine("  FAILED to read PCI config!");
            CloseHandle(h);
            return;
        }

        for (uint off = 0; off < 256; off += 4) {
            uint v = GetConfigDword(cfg, off);
            if (v != 0 && v != 0xFFFFFFFF)
                Console.WriteLine("  config[0x{0:X2}] = 0x{1:X8}", off, v);
        }

        Console.WriteLine("\nStep 2: Test indirect access via config 0xB8/0xBC:\n");
        uint origB8 = GetConfigDword(cfg, 0xB8);
        uint origBC = GetConfigDword(cfg, 0xBC);
        Console.WriteLine("  config[0xB8] = 0x{0:X8}", origB8);
        Console.WriteLine("  config[0xBC] = 0x{0:X8}", origBC);

        // Write addresses to 0xB8, read 0xBC for data
        uint[] testAddrs = {
            0x00000000, 0x000000B8, 0x0000C100, 0x0000C180,
            0x00020004, 0x080000C0, 0x080000C4, 0x00016104, 0x00016284,
        };

        foreach (uint addr in testAddrs) {
            WritePciConfig(h, bus, dev, func, 0xB8, addr);
            System.Threading.Thread.Sleep(1);
            // Re-read config to get fresh 0xBC
            byte[] cfg2 = ReadPciConfig(h, bus, dev, func);
            uint data = GetConfigDword(cfg2, 0xBC);
            Console.WriteLine("  config[0xB8]=0x{0:X8} -> config[0xBC]=0x{1:X8}", addr, data);
        }

        // Restore
        WritePciConfig(h, bus, dev, func, 0xB8, origB8);
        WritePciConfig(h, bus, dev, func, 0xBC, origBC);

        Console.WriteLine("\nStep 3: Try SMN via 0xB8/0xBC:\n");
        // Write SMN_INDEX address to 0xB8
        WritePciConfig(h, bus, dev, func, 0xB8, 0x080000C0);
        System.Threading.Thread.Sleep(1);
        // Write SMN address to 0xBC (data register)
        WritePciConfig(h, bus, dev, func, 0xBC, 0x00008000);
        System.Threading.Thread.Sleep(1);
        byte[] cfg3 = ReadPciConfig(h, bus, dev, func);
        uint smn1 = GetConfigDword(cfg3, 0xBC);
        Console.WriteLine("  SMN[0x00008000] via 0xB8/0xBC = 0x{0:X8}", smn1);

        WritePciConfig(h, bus, dev, func, 0xBC, 0x00020004);
        System.Threading.Thread.Sleep(1);
        byte[] cfg4 = ReadPciConfig(h, bus, dev, func);
        uint smn2 = GetConfigDword(cfg4, 0xBC);
        Console.WriteLine("  SMN[0x00020004] via 0xB8/0xBC = 0x{0:X8}", smn2);

        if (smn1 != smn2 && smn1 != 0xFFFFFFFF && smn2 != 0xFFFFFFFF)
            Console.WriteLine("  *** SMN ACCESS DETECTED! ***");

        Console.WriteLine("\nStep 4: Scan config 0xB8 with 0x08000000-0x080000E0:\n");
        for (uint addr = 0x08000000; addr <= 0x080000E0; addr += 4) {
            WritePciConfig(h, bus, dev, func, 0xB8, addr);
            System.Threading.Thread.Sleep(1);
            byte[] cfgN = ReadPciConfig(h, bus, dev, func);
            uint val = GetConfigDword(cfgN, 0xBC);
            if (val != 0 && val != 0xFFFFFFFF)
                Console.WriteLine("  0xB8=0x{0:X8} -> 0xBC=0x{1:X8}", addr, val);
        }

        CloseHandle(h);
    }
}

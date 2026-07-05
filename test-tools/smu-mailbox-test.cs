using System;
using System.Runtime.InteropServices;
using System.Threading;

class SmuMailboxTest {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sec, uint create, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inb, uint inl, byte[] outb, uint outl, out uint br, IntPtr ov);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const uint IOCTL_READ_REG  = 0x80000B88;
    const uint IOCTL_WRITE_REG = 0x80000B8C;
    const uint IOCTL_INIT_HW   = 0x80000B80;

    // BC-250 SMU v11.8 registers (corrected offsets)
    const uint C2PMSG_66 = 0x16A08;  // message
    const uint C2PMSG_82 = 0x16A48;  // argument / result
    const uint C2PMSG_83 = 0x16A4C;  // extended data
    const uint C2PMSG_90 = 0x16A68;  // response (0=busy, 1=OK, FF=fail)

    // Old (Navi10) SMU offsets for comparison
    const uint OLD_C2PMSG_66 = 0x16104;
    const uint OLD_C2PMSG_82 = 0x16148;
    const uint OLD_C2PMSG_90 = 0x16168;

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

    static uint SendSmuMessage(uint msgId, uint arg, int timeoutMs) {
        // Step 1: Clear response
        WriteReg(C2PMSG_90, 0);
        // Step 2: Write argument
        WriteReg(C2PMSG_82, arg);
        // Step 3: Write message (triggers SMU)
        WriteReg(C2PMSG_66, msgId & 0xFFFF);
        // Step 4: Poll C2PMSG_90
        int elapsed = 0;
        while (elapsed < timeoutMs) {
            uint resp = ReadReg(C2PMSG_90);
            if (resp == 1) return ReadReg(C2PMSG_82); // OK
            if (resp == 0xFF) { Console.WriteLine("  SMU failed (0xFF)"); return 0xFFFFFFFF; }
            if (resp == 0xFE) { Console.WriteLine("  SMU unknown cmd (0xFE)"); return 0xFFFFFFFF; }
            if (resp == 0xFD) { Console.WriteLine("  SMU rejected prereq (0xFD)"); return 0xFFFFFFFF; }
            if (resp == 0xFC) { Console.WriteLine("  SMU rejected busy (0xFC)"); return 0xFFFFFFFF; }
            if (resp != 0) { Console.WriteLine("  SMU unknown response: 0x{0:X2}", resp); return 0xFFFFFFFF; }
            Thread.Sleep(1);
            elapsed += 1;
        }
        Console.WriteLine("  SMU timeout after {0}ms", timeoutMs);
        return 0xFFFFFFFF;
    }

    static void Main() {
        h = CreateFileW(@"\\.\AMDBC250DreamV43", 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (h == (IntPtr)(-1)) {
            Console.WriteLine("Cannot open device: " + Marshal.GetLastWin32Error());
            return;
        }
        Console.WriteLine("=== SMU v11.8 Mailbox Test ===\n");

        // Init hardware
        byte[] initBuf = new byte[32];
        BitConverter.GetBytes((ulong)0xFE800000).CopyTo(initBuf, 0);
        BitConverter.GetBytes((ulong)0x80000).CopyTo(initBuf, 8);
        BitConverter.GetBytes((ulong)0xC0000000).CopyTo(initBuf, 16);
        BitConverter.GetBytes((ulong)0x40000000).CopyTo(initBuf, 24);
        uint br;
        DeviceIoControl(h, IOCTL_INIT_HW, initBuf, 32, null, 0, out br, IntPtr.Zero);
        Console.WriteLine("Hardware initialized.\n");

        // 1. Read all SMU registers at corrected offsets (initial state)
        Console.WriteLine("1. SMU register state (corrected BC-250 offsets):");
        uint r66 = ReadReg(C2PMSG_66);
        uint r82 = ReadReg(C2PMSG_82);
        uint r83 = ReadReg(C2PMSG_83);
        uint r90 = ReadReg(C2PMSG_90);
        Console.WriteLine("   C2PMSG_66 [0x16A08] = 0x{0:X8}  (message)", r66);
        Console.WriteLine("   C2PMSG_82 [0x16A48] = 0x{0:X8}  (argument)", r82);
        Console.WriteLine("   C2PMSG_83 [0x16A4C] = 0x{0:X8}  (ext data)", r83);
        Console.WriteLine("   C2PMSG_90 [0x16A68] = 0x{0:X8}  (response)", r90);

        // 2. Read old (Navi10) offsets for comparison
        Console.WriteLine("\n2. Old Navi10 SMU offsets (for comparison):");
        uint o66 = ReadReg(OLD_C2PMSG_66);
        uint o82 = ReadReg(OLD_C2PMSG_82);
        uint o90 = ReadReg(OLD_C2PMSG_90);
        Console.WriteLine("   C2PMSG_66 [0x16104] = 0x{0:X8}", o66);
        Console.WriteLine("   C2PMSG_82 [0x16148] = 0x{0:X8}", o82);
        Console.WriteLine("   C2PMSG_90 [0x16168] = 0x{0:X8}", o90);

        // 3. Try THM thermal at corrected offset
        Console.WriteLine("\n3. THM thermal sensor (corrected BC-250 offset):");
        // THM_BASE=0x16600, CG_MULT_THERMAL_STATUS = 0x16600 + 0x05F*4 = 0x1677C
        uint temp = ReadReg(0x1677C);
        Console.WriteLine("   CG_MULT_THERMAL_STATUS [0x1677C] = 0x{0:X8}", temp);

        // 4. Send SMU TestMessage
        Console.WriteLine("\n4. SMU TestMessage (0x1):");
        uint result = SendSmuMessage(0x1, 0, 100);
        if (result != 0xFFFFFFFF) {
            Console.WriteLine("   TestMessage OK, result=0x{0:X8}", result);
        }

        // 5. Get SMU version
        Console.WriteLine("\n5. SMU GetSmuVersion (0x2):");
        uint ver = SendSmuMessage(0x2, 0, 100);
        if (ver != 0xFFFFFFFF) {
            uint major = (ver >> 16) & 0xFF;
            uint minor = (ver >> 8) & 0xFF;
            uint patch = ver & 0xFF;
            Console.WriteLine("   SMU Version: {0}.{1}.{2} (raw=0x{3:X8})", major, minor, patch, ver);
        }

        // 6. GetEnabledSmuFeatures
        Console.WriteLine("\n6. SMU GetEnabledSmuFeatures (0x3D):");
        uint features = SendSmuMessage(0x3D, 0, 100);
        if (features != 0xFFFFFFFF) {
            Console.WriteLine("   Enabled features mask: 0x{0:X8}", features);
        }

        // 7. QueryActiveWgp
        Console.WriteLine("\n7. SMU QueryActiveWgp (0x1E):");
        uint wgp = SendSmuMessage(0x1E, 0, 100);
        if (wgp != 0xFFFFFFFF) {
            Console.WriteLine("   Active WGPs: {0} (mask=0x{1:X8})", wgp, wgp);
        }

        // 8. Read SPI_PG_ENABLE_STATIC_WGP_MASK
        Console.WriteLine("\n8. SPI_PG_ENABLE_STATIC_WGP_MASK [0x34FC] = 0x{0:X8}", ReadReg(0x34FC));

        Console.WriteLine("\n=== SMU Test Complete ===");
        CloseHandle(h);
    }
}

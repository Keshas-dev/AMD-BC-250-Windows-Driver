using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

class KiqAfterMecTest {
    const uint IOCTL_WRITE_REG = 0x80000BD4;
    const uint IOCTL_READ_REG = 0x80000BD5;
    const uint IOCTL_LOAD_CP_FW = 0x80000BD6;
    
    static IntPtr handle;
    
    static unsafe void Main(string[] args) {
        string device = @"\\.\AMDBC250DreamV43";
        handle = CreateFile(device, 0x80000000 | 0x40000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (handle == new IntPtr(-1)) {
            Console.WriteLine("Failed to open driver: " + Marshal.GetLastWin32Error());
            return;
        }
        
        Console.WriteLine("=== KIQ After MEC Firmware Load Test ===");
        Console.WriteLine();
        
        // Read current SCRATCH value
        uint scratchBefore = ReadReg(0x32D4);
        Console.WriteLine($"SCRATCH before: 0x{scratchBefore:X8}");
        
        // Load MEC firmware if needed
        Console.WriteLine("Loading MEC firmware...");
        byte[] fw = File.ReadAllBytes(@"C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\firmware\cyan_skillfish2_mec.bin");
        LoadCpFw(fw, 4);
        Console.WriteLine("MEC firmware loaded.");
        
        // Now try KIQ setup workaround
        Console.WriteLine();
        Console.WriteLine("=== KIQ Setup Workaround ===");
        
        // Select ME=1 (GRBM_GFX_INDEX = 0x34D0)
        WriteReg(0x34D0, 0xE0000000 | (1 << 16)); // ME=1
        
        // KIQ_BASE_LO
        WriteReg(0xE060, 0xDEADBEEF);
        WriteReg(0xE064, 0x00000000); // HI - read-only but try
        Console.WriteLine($"KIQ_BASE_LO wrote 0xDEADBEEF, read: 0x{ReadReg(0xE060):X8}");
        Console.WriteLine($"KIQ_BASE_HI wrote 0x00000000, read: 0x{ReadReg(0xE064):X8}");
        
        // KIQ_SIZE - read-only, reads 0
        uint kiqSize = ReadReg(0xE068);
        Console.WriteLine($"KIQ_SIZE (0xE068): 0x{kiqSize:X8} (READ-ONLY)");
        
        // KIQ_RPTR
        WriteReg(0xE06C, 0x00000000);
        Console.WriteLine($"KIQ_RPTR wrote 0x0, read: 0x{ReadReg(0xE06C):X8}");
        
        // KIQ_WPTR
        WriteReg(0xE078, 0x00000002);
        Console.WriteLine($"KIQ_WPTR wrote 0x2, read: 0x{ReadReg(0xE078):X8}");
        
        // KIQ_DOORBELL
        WriteReg(0xE074, 0x00000001);
        Console.WriteLine($"KIQ_DOORBELL wrote 0x1, read: 0x{ReadReg(0xE074):X8}");
        
        // KIQ_VMID
        WriteReg(0xE07C, 0x00000000);
        Console.WriteLine($"KIQ_VMID wrote 0x0, read: 0x{ReadReg(0xE07C):X8}");
        
        // KIQ_ACTIVE - try to activate
        WriteReg(0xE080, 0x00000001);
        Console.WriteLine($"KIQ_ACTIVE wrote 0x1, read: 0x{ReadReg(0xE080):X8}");
        
        // Wait a bit
        Thread.Sleep(100);
        
        // Check RPTR again
        uint rptrAfter = ReadReg(0xE06C);
        Console.WriteLine();
        Console.WriteLine($"KIQ_RPTR after 100ms: 0x{rptrAfter:X8}");
        
        // Check GRBM_STATUS
        uint grbmStatus = ReadReg(0x2010);
        Console.WriteLine($"GRBM_STATUS (0x2010): 0x{grbmStatus:X8}");
        
        // Restore GRBM_GFX_INDEX to broadcast
        WriteReg(0x34D0, 0xE0000000);
        
        // Deactivate KIQ
        WriteReg(0xE080, 0x00000000);
        
        Console.WriteLine();
        Console.WriteLine("=== Test Complete ===");
        
        CloseHandle(handle);
    }
    
    static uint ReadReg(uint offset) {
        uint bytesReturned;
        uint value = 0;
        byte[] inBuffer = new byte[8];
        byte[] outBuffer = new byte[4];
        
        Buffer.BlockCopy(BitConverter.GetBytes(offset), 0, inBuffer, 0, 4);
        Buffer.BlockCopy(BitConverter.GetBytes((uint)1), 0, inBuffer, 4, 4); // engine=1
        
        bool result = DeviceIoControl(handle, IOCTL_READ_REG, inBuffer, (uint)inBuffer.Length,
            outBuffer, (uint)outBuffer.Length, out bytesReturned, IntPtr.Zero);
        
        if (result && bytesReturned == 4) {
            value = BitConverter.ToUInt32(outBuffer, 0);
        }
        return value;
    }
    
    static void WriteReg(uint offset, uint value) {
        uint bytesReturned;
        byte[] inBuffer = new byte[16];
        
        Buffer.BlockCopy(BitConverter.GetBytes(offset), 0, inBuffer, 0, 4);
        Buffer.BlockCopy(BitConverter.GetBytes(value), 0, inBuffer, 4, 4);
        Buffer.BlockCopy(BitConverter.GetBytes((uint)1), 0, inBuffer, 8, 4); // engine=1
        Buffer.BlockCopy(BitConverter.GetBytes((uint)1), 0, inBuffer, 12, 4); // write=1
        
        DeviceIoControl(handle, IOCTL_WRITE_REG, inBuffer, (uint)inBuffer.Length, null, 0, out bytesReturned, IntPtr.Zero);
    }
    
    static void LoadCpFw(byte[] fw, uint type) {
        uint bytesReturned;
        byte[] inBuffer = new byte[fw.Length + 8];
        
        Buffer.BlockCopy(BitConverter.GetBytes((uint)type), 0, inBuffer, 0, 4);
        Buffer.BlockCopy(BitConverter.GetBytes((uint)0), 0, inBuffer, 4, 4);
        Buffer.BlockCopy(fw, 0, inBuffer, 8, fw.Length);
        
        DeviceIoControl(handle, IOCTL_LOAD_CP_FW, inBuffer, (uint)inBuffer.Length, null, 0, out bytesReturned, IntPtr.Zero);
    }
    
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);
    
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(IntPtr hDevice, uint dwIoControlCode, byte[] lpInBuffer, uint nInBufferSize,
        byte[] lpOutBuffer, uint nOutBufferSize, out uint lpBytesReturned, IntPtr lpOverlapped);
    
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr hObject);
}

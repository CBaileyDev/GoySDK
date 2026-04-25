using System.Runtime.InteropServices;
using System.Text;

namespace GoyLoader.Native;

internal static class NativeMethods
{
    public const uint MemCommit = 0x1000;
    public const uint MemReserve = 0x2000;
    public const uint PageReadwrite = 0x04;
    public const uint Infinite = 0xFFFFFFFF;

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr GetModuleHandleW(string? lpModuleName);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi, ExactSpelling = true)]
    public static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, uint dwFreeType);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, uint nSize, out UIntPtr lpNumberOfBytesWritten);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, out IntPtr lpThreadId);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetExitCodeThread(IntPtr hThread, out IntPtr lpExitCode);

    [DllImport("psapi.dll", SetLastError = true)]
    public static extern bool EnumProcessModulesEx(IntPtr hProcess, [Out] IntPtr[] lphModule, uint cb, out uint lpcbNeeded, uint dwFilterFlag);

    [DllImport("psapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern uint GetModuleFileNameExW(IntPtr hProcess, IntPtr hModule, [Out] System.Text.StringBuilder lpFilename, uint nSize);

    public const uint ProcessCreateThread = 0x0002;
    public const uint ProcessQueryInformation = 0x0400;
    public const uint ProcessVmOperation = 0x0008;
    public const uint ProcessVmWrite = 0x0020;
    public const uint ProcessVmRead = 0x0010;
    public static readonly uint ProcessInjectAccess = ProcessCreateThread | ProcessQueryInformation | ProcessVmOperation | ProcessVmWrite | ProcessVmRead;

    public const uint LIST_MODULES_DEFAULT = 0x03; // LIST_MODULES_32BIT | LIST_MODULES_64BIT

    public const uint MemRelease = 0x8000;

    public static byte[] Utf16NullTerminated(string path)
    {
        return Encoding.Unicode.GetBytes(path + "\0");
    }
}

using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using GoyLoader.Native;

namespace GoyLoader.Services;

public static class DllInjector
{
    public static IntPtr InjectLoadLibrary(int processId, string absoluteDllPath)
    {
        if (!Path.IsPathRooted(absoluteDllPath))
            throw new ArgumentException("DLL path must be absolute.", nameof(absoluteDllPath));
        if (!File.Exists(absoluteDllPath))
            throw new FileNotFoundException("DLL not found.", absoluteDllPath);

        var pathBytes = NativeMethods.Utf16NullTerminated(absoluteDllPath);
        var hProcess = NativeMethods.OpenProcess(NativeMethods.ProcessInjectAccess, false, processId);
        if (hProcess == IntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "OpenProcess failed (try running the loader as Administrator).");

        IntPtr remoteMem = IntPtr.Zero;
        IntPtr hThread = IntPtr.Zero;
        try
        {
            remoteMem = NativeMethods.VirtualAllocEx(
                hProcess,
                IntPtr.Zero,
                (uint)pathBytes.Length,
                NativeMethods.MemCommit | NativeMethods.MemReserve,
                NativeMethods.PageReadwrite);
            if (remoteMem == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "VirtualAllocEx failed.");

            if (!NativeMethods.WriteProcessMemory(hProcess, remoteMem, pathBytes, (uint)pathBytes.Length, out _))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "WriteProcessMemory failed.");

            var k32 = NativeMethods.GetModuleHandleW("kernel32.dll");
            if (k32 == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "GetModuleHandle(kernel32) failed.");

            var loadLib = NativeMethods.GetProcAddress(k32, "LoadLibraryW");
            if (loadLib == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "GetProcAddress(LoadLibraryW) failed.");

            hThread = NativeMethods.CreateRemoteThread(hProcess, IntPtr.Zero, 0, loadLib, remoteMem, 0, out _);
            if (hThread == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateRemoteThread failed.");

            var wait = NativeMethods.WaitForSingleObject(hThread, 30_000);
            if (wait != 0)
                throw new TimeoutException("Remote LoadLibraryW did not complete in time.");

            if (!NativeMethods.GetExitCodeThread(hThread, out var exitCode) || exitCode == IntPtr.Zero)
                throw new InvalidOperationException("LoadLibraryW returned NULL in the target process (missing dependencies or wrong architecture).");

            return exitCode;
        }
        finally
        {
            if (hThread != IntPtr.Zero)
                NativeMethods.CloseHandle(hThread);
            if (remoteMem != IntPtr.Zero)
                NativeMethods.VirtualFreeEx(hProcess, remoteMem, 0, NativeMethods.MemRelease);
            NativeMethods.CloseHandle(hProcess);
        }
    }

    public static bool IsModuleLoaded(int processId, string moduleFileName)
    {
        var hProcess = NativeMethods.OpenProcess(NativeMethods.ProcessQueryInformation | NativeMethods.ProcessVmRead, false, processId);
        if (hProcess == IntPtr.Zero)
            return false;

        try
        {
            uint needed = 0;
            // first call to get needed size
            NativeMethods.EnumProcessModulesEx(hProcess, null, 0, out needed, NativeMethods.LIST_MODULES_DEFAULT);
            if (needed == 0)
                return false;

            var count = needed / (uint)IntPtr.Size;
            var modules = new IntPtr[count];
            if (!NativeMethods.EnumProcessModulesEx(hProcess, modules, needed, out needed, NativeMethods.LIST_MODULES_DEFAULT))
                return false;

            var sb = new System.Text.StringBuilder(260);
            foreach (var m in modules)
            {
                sb.Clear();
                NativeMethods.GetModuleFileNameExW(hProcess, m, sb, (uint)sb.Capacity);
                if (sb.Length == 0)
                    continue;
                if (Path.GetFileName(sb.ToString()).Equals(moduleFileName, StringComparison.OrdinalIgnoreCase))
                    return true;
            }

            return false;
        }
        finally
        {
            NativeMethods.CloseHandle(hProcess);
        }
    }

    public static string FormatProcessLine(Process p)
    {
        try
        {
            return $"{p.ProcessName}.exe (PID {p.Id})";
        }
        catch
        {
            return $"PID {p.Id}";
        }
    }
}

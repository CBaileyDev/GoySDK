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

            // P2/03: handle each WaitForSingleObject result code distinctly so the
            // user sees an accurate failure mode instead of a generic timeout.
            var wait = NativeMethods.WaitForSingleObject(hThread, 30_000);
            switch (wait)
            {
                case NativeMethods.WaitObject0:
                    break;
                case NativeMethods.WaitTimeout:
                    throw new TimeoutException("Remote LoadLibraryW did not complete in 30 seconds.");
                case NativeMethods.WaitFailed:
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "WaitForSingleObject failed.");
                default:
                    throw new InvalidOperationException($"Unexpected WaitForSingleObject result: 0x{wait:X8}.");
            }

            // P2/03: GetExitCodeThread truncates the HMODULE to 32 bits on x64,
            // so we treat its return only as a "non-zero == LoadLibrary returned
            // a valid handle" signal. For the actual handle, enumerate modules.
            if (!NativeMethods.GetExitCodeThread(hThread, out var exitCode))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "GetExitCodeThread failed.");
            if (exitCode == 0)
                throw new InvalidOperationException("LoadLibraryW returned NULL in the target process (missing dependencies or wrong architecture).");

            var realHandle = FindModuleHandle(processId, absoluteDllPath);
            if (realHandle == IntPtr.Zero)
                throw new InvalidOperationException(
                    $"LoadLibraryW reported success but '{Path.GetFileName(absoluteDllPath)}' was not found in the target's module list.");

            return realHandle;
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
        return FindModuleHandle(processId, moduleFileName) != IntPtr.Zero;
    }

    /// <summary>
    /// P2/03: enumerate the target process's modules and return the full HMODULE
    /// for one whose path matches <paramref name="expectedDllPathOrName"/>. Pass
    /// either an absolute path (preferred — matched by canonicalized full path)
    /// or a bare file name (matched case-insensitive against each module's name).
    /// Returns IntPtr.Zero if no match was found or the target couldn't be opened.
    /// </summary>
    public static IntPtr FindModuleHandle(int processId, string expectedDllPathOrName)
    {
        var expectedFullPath = Path.IsPathRooted(expectedDllPathOrName)
            ? Path.GetFullPath(expectedDllPathOrName)
            : null;
        var expectedFileName = Path.GetFileName(expectedDllPathOrName);
        if (string.IsNullOrWhiteSpace(expectedFileName))
            return IntPtr.Zero;

        var hProcess = NativeMethods.OpenProcess(
            NativeMethods.ProcessQueryInformation | NativeMethods.ProcessVmRead,
            false, processId);
        if (hProcess == IntPtr.Zero)
            return IntPtr.Zero;

        try
        {
            uint needed = 0;
            // P2/04: EnumProcessModulesEx now declares lphModule nullable; passing
            // null is the documented size-discovery pattern.
            NativeMethods.EnumProcessModulesEx(hProcess, (IntPtr[]?)null, 0, out needed, NativeMethods.LIST_MODULES_DEFAULT);
            if (needed == 0)
                return IntPtr.Zero;

            var count = needed / (uint)IntPtr.Size;
            var modules = new IntPtr[count];
            if (!NativeMethods.EnumProcessModulesEx(hProcess, modules, needed, out needed, NativeMethods.LIST_MODULES_DEFAULT))
                return IntPtr.Zero;

            var sb = new System.Text.StringBuilder(1024);
            foreach (var module in modules)
            {
                sb.Clear();
                NativeMethods.GetModuleFileNameExW(hProcess, module, sb, (uint)sb.Capacity);
                if (sb.Length == 0)
                    continue;

                var modulePath = sb.ToString();
                if (expectedFullPath != null)
                {
                    try
                    {
                        if (string.Equals(
                                Path.GetFullPath(modulePath),
                                expectedFullPath,
                                StringComparison.OrdinalIgnoreCase))
                        {
                            return module;
                        }
                    }
                    catch
                    {
                        // Fall back to file-name match below.
                    }
                }

                if (string.Equals(
                        Path.GetFileName(modulePath),
                        expectedFileName,
                        StringComparison.OrdinalIgnoreCase))
                {
                    return module;
                }
            }

            return IntPtr.Zero;
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

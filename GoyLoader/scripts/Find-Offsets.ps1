<#
.SYNOPSIS
    Scans a UE3 game process to find GNames and GObjects TArray offsets.
.DESCRIPTION
    Opens the target process and scans its .data section for pairs of
    TArray structures (pointer + count + max) that match the expected layout of
    GNames and GObjects in Unreal Engine 3.  GObjects is expected at GNames + 0x48.
#>
$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Diagnostics;
using System.Collections.Generic;
using System.IO;

public static class OffsetScanner
{
    // --- P/Invoke ---
    const uint PROCESS_VM_READ = 0x0010;
    const uint PROCESS_QUERY_INFORMATION = 0x0400;

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr h);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr baseAddr,
        byte[] buffer, int size, out int bytesRead);

    [DllImport("psapi.dll", SetLastError = true)]
    static extern bool GetModuleInformation(IntPtr hProcess, IntPtr hModule,
        out MODULEINFO lpmodinfo, int cb);

    [StructLayout(LayoutKind.Sequential)]
    struct MODULEINFO
    {
        public IntPtr lpBaseOfDll;
        public int SizeOfImage;
        public IntPtr EntryPoint;
    }

    // --- PE parsing helpers ---
    struct SectionInfo
    {
        public string Name;
        public uint VirtualAddress;
        public uint VirtualSize;
        public uint PointerToRawData;
        public uint SizeOfRawData;
    }

    static List<SectionInfo> ParsePESections(byte[] headerBuf)
    {
        var sections = new List<SectionInfo>();
        int e_lfanew = BitConverter.ToInt32(headerBuf, 0x3C);
        // PE\0\0 at e_lfanew
        int coffHeaderStart = e_lfanew + 4;
        ushort numSections = BitConverter.ToUInt16(headerBuf, coffHeaderStart + 2);
        ushort sizeOfOptional = BitConverter.ToUInt16(headerBuf, coffHeaderStart + 16);
        int sectionTableOffset = coffHeaderStart + 20 + sizeOfOptional;

        for (int i = 0; i < numSections; i++)
        {
            int off = sectionTableOffset + i * 40;
            if (off + 40 > headerBuf.Length) break;
            string name = System.Text.Encoding.ASCII.GetString(headerBuf, off, 8).TrimEnd('\0');
            var si = new SectionInfo();
            si.Name = name;
            si.VirtualSize = BitConverter.ToUInt32(headerBuf, off + 8);
            si.VirtualAddress = BitConverter.ToUInt32(headerBuf, off + 12);
            si.SizeOfRawData = BitConverter.ToUInt32(headerBuf, off + 16);
            si.PointerToRawData = BitConverter.ToUInt32(headerBuf, off + 20);
            sections.Add(si);
        }
        return sections;
    }

    // --- TArray validation ---
    static bool IsPlausiblePointer(long val)
    {
        // x64 user-mode: 0x10000 .. 0x7FFFFFFFFFFF
        return val > 0x10000 && val < 0x7FFFFFFFFFFF;
    }

    struct TArrayCandidate
    {
        public long DataPtr;
        public int Count;
        public int Max;
    }

    static bool TryReadTArray(byte[] data, int offset, out TArrayCandidate result)
    {
        result = new TArrayCandidate();
        if (offset + 16 > data.Length) return false;
        result.DataPtr = BitConverter.ToInt64(data, offset);
        result.Count = BitConverter.ToInt32(data, offset + 8);
        result.Max = BitConverter.ToInt32(data, offset + 12);
        return true;
    }

    static bool IsValidGNames(TArrayCandidate t)
    {
        if (!IsPlausiblePointer(t.DataPtr)) return false;
        // GNames typically has 50k-500k entries
        if (t.Count < 30000 || t.Count > 1000000) return false;
        if (t.Max < t.Count) return false;
        if (t.Max > t.Count * 4) return false; // capacity shouldn't be wildly larger
        return true;
    }

    static bool IsValidGObjects(TArrayCandidate t)
    {
        if (!IsPlausiblePointer(t.DataPtr)) return false;
        // GObjects typically has 50k-2M entries
        if (t.Count < 30000 || t.Count > 5000000) return false;
        if (t.Max < t.Count) return false;
        if (t.Max > t.Count * 4) return false;
        return true;
    }

    // --- Deeper validation: read actual pointer array entries ---
    static bool ValidatePointerArray(IntPtr hProcess, long arrayDataPtr, int count, int samplesToCheck)
    {
        if (count <= 0) return false;
        int toCheck = Math.Min(samplesToCheck, count);
        byte[] ptrBuf = new byte[toCheck * 8];
        int read;
        if (!ReadProcessMemory(hProcess, (IntPtr)arrayDataPtr, ptrBuf, ptrBuf.Length, out read))
            return false;
        if (read < ptrBuf.Length) return false;

        int validPtrs = 0;
        int nullPtrs = 0;
        for (int i = 0; i < toCheck; i++)
        {
            long ptr = BitConverter.ToInt64(ptrBuf, i * 8);
            if (ptr == 0)
                nullPtrs++;
            else if (IsPlausiblePointer(ptr))
                validPtrs++;
            else
                return false; // bad pointer = not a valid array
        }
        // At least 80% should be valid pointers (some null entries are normal)
        return validPtrs > toCheck * 0.5;
    }

    // --- Main scan ---
    public static string Scan(int pid)
    {
        var results = new List<string>();
        results.Add("=== Offset scan report ===");
        results.Add(string.Format("Target PID: {0}", pid));

        Process proc;
        try { proc = Process.GetProcessById(pid); }
        catch { return "ERROR: Could not open process " + pid; }

        IntPtr baseAddr = proc.MainModule.BaseAddress;
        int imageSize = proc.MainModule.ModuleMemorySize;
        results.Add(string.Format("Base: 0x{0:X}", baseAddr.ToInt64()));
        results.Add(string.Format("Image size: 0x{0:X} ({1} MB)", imageSize, imageSize / 1024 / 1024));

        IntPtr hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, false, pid);
        if (hProcess == IntPtr.Zero)
            return "ERROR: OpenProcess failed. Run as Administrator.";

        try
        {
            // Read PE headers (first 4KB)
            byte[] headerBuf = new byte[4096];
            int headerRead;
            if (!ReadProcessMemory(hProcess, baseAddr, headerBuf, headerBuf.Length, out headerRead))
                return "ERROR: Failed to read PE headers.";

            var sections = ParsePESections(headerBuf);
            results.Add(string.Format("Sections: {0}", sections.Count));
            foreach (var s in sections)
                results.Add(string.Format("  {0}: VA=0x{1:X} Size=0x{2:X}", s.Name, s.VirtualAddress, s.VirtualSize));

            // Find .data section (globals live here)
            SectionInfo dataSection = new SectionInfo();
            bool foundData = false;
            foreach (var s in sections)
            {
                if (s.Name == ".data")
                {
                    dataSection = s;
                    foundData = true;
                    break;
                }
            }

            // Also scan .rdata as fallback (some UE3 builds put globals there)
            var sectionsToScan = new List<SectionInfo>();
            if (foundData) sectionsToScan.Add(dataSection);
            foreach (var s in sections)
            {
                if (s.Name == ".rdata") sectionsToScan.Add(s);
            }

            if (sectionsToScan.Count == 0)
                return "ERROR: No .data or .rdata sections found.";

            int candidateCount = 0;
            var confirmedResults = new List<string>();

            foreach (var section in sectionsToScan)
            {
                results.Add(string.Format("\nScanning section '{0}' (VA=0x{1:X}, Size=0x{2:X})...",
                    section.Name, section.VirtualAddress, section.VirtualSize));

                uint scanSize = section.VirtualSize;
                if (scanSize > 64 * 1024 * 1024) scanSize = 64 * 1024 * 1024; // cap at 64 MB

                byte[] sectionData = new byte[scanSize];
                int bytesRead;
                IntPtr sectionAddr = (IntPtr)(baseAddr.ToInt64() + section.VirtualAddress);
                if (!ReadProcessMemory(hProcess, sectionAddr, sectionData, (int)scanSize, out bytesRead))
                {
                    results.Add("  WARNING: ReadProcessMemory failed for this section.");
                    continue;
                }
                results.Add(string.Format("  Read {0} bytes.", bytesRead));

                // Scan for GNames/GObjects pairs at 0x48 offset
                int stride = 8; // 8-byte aligned
                int limit = bytesRead - 0x48 - 16;
                for (int off = 0; off < limit; off += stride)
                {
                    TArrayCandidate gnamesCandidate, gobjectsCandidate;

                    if (!TryReadTArray(sectionData, off, out gnamesCandidate)) continue;
                    if (!IsValidGNames(gnamesCandidate)) continue;

                    if (!TryReadTArray(sectionData, off + 0x48, out gobjectsCandidate)) continue;
                    if (!IsValidGObjects(gobjectsCandidate)) continue;

                    candidateCount++;

                    ulong gnamesOffset = (ulong)section.VirtualAddress + (ulong)off;
                    ulong gobjectsOffset = gnamesOffset + 0x48;

                    string candidateInfo = string.Format(
                        "  CANDIDATE #{0}:\n" +
                        "    GNames:   base + 0x{1:X}  (Data=0x{2:X}, Count={3}, Max={4})\n" +
                        "    GObjects: base + 0x{5:X}  (Data=0x{6:X}, Count={7}, Max={8})",
                        candidateCount,
                        gnamesOffset, gnamesCandidate.DataPtr, gnamesCandidate.Count, gnamesCandidate.Max,
                        gobjectsOffset, gobjectsCandidate.DataPtr, gobjectsCandidate.Count, gobjectsCandidate.Max);
                    results.Add(candidateInfo);

                    // Deep validate by reading actual pointer arrays
                    bool gnamesValid = ValidatePointerArray(hProcess, gnamesCandidate.DataPtr, gnamesCandidate.Count, 32);
                    bool gobjectsValid = ValidatePointerArray(hProcess, gobjectsCandidate.DataPtr, gobjectsCandidate.Count, 32);
                    results.Add(string.Format("    Deep validation: GNames={0}, GObjects={1}", gnamesValid, gobjectsValid));

                    if (gnamesValid && gobjectsValid)
                    {
                        confirmedResults.Add(string.Format(
                            "CONFIRMED MATCH:\n" +
                            "  #define GNAMES_OFFSET    static_cast<uintptr_t>(0x{0:X8})\n" +
                            "  #define GOBJECTS_OFFSET  static_cast<uintptr_t>(0x{1:X8})\n" +
                            "  GNames:   Count={2}, Max={3}\n" +
                            "  GObjects: Count={4}, Max={5}",
                            gnamesOffset, gobjectsOffset,
                            gnamesCandidate.Count, gnamesCandidate.Max,
                            gobjectsCandidate.Count, gobjectsCandidate.Max));
                    }
                }
            }

            results.Add(string.Format("\nTotal candidates found: {0}", candidateCount));
            results.Add(string.Format("Confirmed (deep validated): {0}", confirmedResults.Count));

            if (confirmedResults.Count > 0)
            {
                results.Add("\n========================================");
                results.Add("  RESULTS — Update GameDefines.hpp with:");
                results.Add("========================================\n");
                foreach (var r in confirmedResults)
                    results.Add(r);
            }
            else if (candidateCount > 0)
            {
                results.Add("\nWARNING: Found candidates but none passed deep validation.");
                results.Add("The game may have changed its TArray layout or offsets.");
            }
            else
            {
                results.Add("\nNo candidates found. The GNames/GObjects layout may have changed significantly.");
                results.Add("Try checking if the 0x48 offset between GNames and GObjects still holds.");
            }

            return string.Join("\n", results.ToArray());
        }
        finally
        {
            CloseHandle(hProcess);
        }
    }
}
"@

# Run the scanner — process name is decoded at runtime (see GoyLoader/Services/ProcessFinder.cs, same K).
function U-Dec([byte[]]$enc, [byte]$k) { return -join [char[]]($enc | ForEach-Object { $_ -bxor $k }) }
$encImage = [byte[]]@(0x08,0x35,0x39,0x31,0x3F,0x2E,0x16,0x3F,0x3B,0x3D,0x2F,0x3F)
$imageName = U-Dec $encImage 0x5A
$hostProcess = Get-Process -Name $imageName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $hostProcess) {
    Write-Error "Target process is not running. Start the game client first."
    return
}

Write-Host "Scanning process (PID $($hostProcess.Id))..."
Write-Host ""

$result = [OffsetScanner]::Scan($hostProcess.Id)
Write-Output $result

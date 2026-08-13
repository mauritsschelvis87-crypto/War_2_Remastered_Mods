# Live-dump van twee kandidaat-tinttabellen in Warcraft II.exe.
# Alleen lezen — verandert niets aan het spel.
param()

$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Mem {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int size, out int read);
    [DllImport("kernel32.dll")] public static extern int VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, int size);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [StructLayout(LayoutKind.Sequential)]
    public struct MBI {
        public IntPtr BaseAddress; public IntPtr AllocationBase; public uint AllocationProtect;
        public IntPtr RegionSize; public uint State; public uint Protect; public uint Type;
    }
}
"@

$proc = Get-Process "Warcraft II" -ErrorAction Stop
$h = [Mem]::OpenProcess(0x0410, $false, $proc.Id) # QUERY | VM_READ
if ($h -eq [IntPtr]::Zero) { throw "OpenProcess failed" }

$module = $proc.MainModule
"module base=0x{0:X8} size=0x{1:X} path={2}" -f $module.BaseAddress.ToInt32(), $module.ModuleMemorySize, $module.FileName

foreach ($addr in @(0x00779640, 0x00D29640)) {
    $mbi = New-Object Mem+MBI
    [void][Mem]::VirtualQueryEx($h, [IntPtr]$addr, [ref]$mbi, [Runtime.InteropServices.Marshal]::SizeOf([type][Mem+MBI]))
    $inModule = ($addr -ge $module.BaseAddress.ToInt32()) -and ($addr -lt ($module.BaseAddress.ToInt32() + $module.ModuleMemorySize))
    "--- 0x{0:X8}  regionBase=0x{1:X8} allocBase=0x{2:X8} protect=0x{3:X} type=0x{4:X} inExeImage={5}" -f `
        $addr, $mbi.BaseAddress.ToInt32(), $mbi.AllocationBase.ToInt32(), $mbi.Protect, $mbi.Type, $inModule
    $buf = New-Object byte[] 128
    $read = 0
    if ([Mem]::ReadProcessMemory($h, [IntPtr]$addr, $buf, 128, [ref]$read) -and $read -eq 128) {
        for ($p = 0; $p -lt 8; $p++) {
            $r = [BitConverter]::ToSingle($buf, $p*16 + 0)
            $g = [BitConverter]::ToSingle($buf, $p*16 + 4)
            $b = [BitConverter]::ToSingle($buf, $p*16 + 8)
            $a = [BitConverter]::ToSingle($buf, $p*16 + 12)
            "  P{0}: R={1:F3} G={2:F3} B={3:F3} A={4:F3}  (~#{5:X2}{6:X2}{7:X2})" -f `
                ($p+1), $r, $g, $b, $a, [int][Math]::Min(255,[Math]::Max(0,$r*255)), [int][Math]::Min(255,[Math]::Max(0,$g*255)), [int][Math]::Min(255,[Math]::Max(0,$b*255))
        }
    } else {
        "  <read failed>"
    }
}
[void][Mem]::CloseHandle($h)

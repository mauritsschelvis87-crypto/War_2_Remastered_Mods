# Live read-only dump: echte werk-tinttabel (RVA 0x929640) + seat->kleur tabel (RVA 0x519390).
$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class M2 {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool i, int p);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, int s, out int r);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
"@

$proc = Get-Process "Warcraft II"
$base = $proc.MainModule.BaseAddress.ToInt32()
"module base=0x{0:X8}" -f $base
$h = [M2]::OpenProcess(0x0410, $false, $proc.Id)

$work = $base + 0x929640
$buf = New-Object byte[] 128
$r = 0
if ([M2]::ReadProcessMemory($h, [IntPtr]$work, $buf, 128, [ref]$r) -and $r -eq 128) {
    "working table @ 0x{0:X8}:" -f $work
    for ($p = 0; $p -lt 8; $p++) {
        $rr = [BitConverter]::ToSingle($buf, $p*16 + 0)
        $gg = [BitConverter]::ToSingle($buf, $p*16 + 4)
        $bb = [BitConverter]::ToSingle($buf, $p*16 + 8)
        "  P{0}: ~#{1:X2}{2:X2}{3:X2}" -f ($p+1),
            [int][Math]::Min(255, [Math]::Max(0, $rr*255)),
            [int][Math]::Min(255, [Math]::Max(0, $gg*255)),
            [int][Math]::Min(255, [Math]::Max(0, $bb*255))
    }
} else {
    "working table read failed @ 0x{0:X8}" -f $work
}

$seat = $base + 0x519390
$sb = New-Object byte[] 8
if ([M2]::ReadProcessMemory($h, [IntPtr]$seat, $sb, 8, [ref]$r) -and $r -eq 8) {
    "seat table @ 0x{0:X8}: [{1}]" -f $seat, (($sb | ForEach-Object { $_ }) -join ',')
} else {
    "seat table read failed"
}
[void][M2]::CloseHandle($h)

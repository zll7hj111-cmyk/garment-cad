param([string]$Keys)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)]
  public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)]
  public struct INPUT {
    [FieldOffset(0)] public uint type;
    [FieldOffset(4)] public KEYBDINPUT ki;
  }
  [DllImport("user32.dll", SetLastError=true)]
  public static extern uint SendInput(uint n, INPUT[] p, int cb);
  public static uint SendKey(ushort vk, bool up) {
    INPUT[] inp = new INPUT[1];
    inp[0].type = 1;
    inp[0].ki.wVk = vk;
    inp[0].ki.dwFlags = up ? 0x0002u : 0u;
    return SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
[void][Native]::SetProcessDpiAwarenessContext([Native]::PMV2)
$proc = Get-Process -Name GarmentCAD -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc) { [void][Native]::SetForegroundWindow($proc.MainWindowHandle); Start-Sleep -Milliseconds 150 }
$map = @{ 'ctrl'=0x11; 'shift'=0x10; 'alt'=0x12; 'z'=0x5A; 'y'=0x59; 'del'=0x2E; 'esc'=0x1B; 'enter'=0x0D }
$parts = $Keys -split '\+'
$down = @(); 
foreach ($p in $parts) { $down += $map[$p.ToLower()] }
$total = 0
foreach ($vk in $down) { $total += [Native]::SendKey($vk, $false); Start-Sleep -Milliseconds 30 }
for ($i = $down.Count - 1; $i -ge 0; $i--) { $total += [Native]::SendKey($down[$i], $true); Start-Sleep -Milliseconds 30 }
Write-Output ("KEYS {0} sent={1}" -f $Keys, $total)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int nIndex);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT { public uint type; public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [DllImport("user32.dll", SetLastError=true)]
  public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
  public static void Send(uint flags, int nx, int ny) {
    INPUT[] inp = new INPUT[1];
    inp[0].type = 0; inp[0].dx = nx; inp[0].dy = ny; inp[0].dwFlags = flags;
    SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
  }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
}
"@
$ok = [Native]::SetProcessDpiAwarenessContext([Native]::PMV2)
Write-Output ("SetDpiAwarenessContext returned: {0}" -f $ok)
$vw = [Native]::GetSystemMetrics(78); $vh = [Native]::GetSystemMetrics(79)
Write-Output ("VirtualScreen: {0}x{1}" -f $vw, $vh)
$tx = 651; $ty = 253
$nx = [int](($tx * 65535.0) / ($vw - 1))
$ny = [int](($ty * 65535.0) / ($vh - 1))
[Native]::Send(0x8001, $nx, $ny)
Start-Sleep -Milliseconds 300
$cp = New-Object Native+POINT
[void][Native]::GetCursorPos([ref]$cp)
Write-Output ("Target {0},{1} -> normalized {2},{3} -> actual cursor {4},{5}" -f $tx, $ty, $nx, $ny, $cp.X, $cp.Y)

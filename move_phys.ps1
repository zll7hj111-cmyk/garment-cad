param([int]$X, [int]$Y)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int nIndex);
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT { public uint type; public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [DllImport("user32.dll", SetLastError=true)]
  public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
  public static void Send(uint flags, int nx, int ny) {
    INPUT[] inp = new INPUT[1];
    inp[0].type = 0; inp[0].dx = nx; inp[0].dy = ny; inp[0].dwFlags = flags;
    SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
[void][Native]::SetProcessDpiAwarenessContext([Native]::PMV2)
$vw = [Native]::GetSystemMetrics(78); $vh = [Native]::GetSystemMetrics(79)
$vx = [Native]::GetSystemMetrics(76); $vy = [Native]::GetSystemMetrics(77)
$nx = [int]((($X - $vx) * 65535.0) / ($vw - 1))
$ny = [int]((($Y - $vy) * 65535.0) / ($vh - 1))
[Native]::Send(0x8001, $nx, $ny)
Write-Output ("MOVED physical {0},{1}" -f $X, $Y)

param([int]$X, [int]$Y)
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern IntPtr ChildWindowFromPointEx(IntPtr h, POINT p, uint flags);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
}
"@
[void][Native]::SetProcessDpiAwarenessContext([Native]::PMV2)
$p = New-Object Native+POINT
$p.X = $X; $p.Y = $Y
$h = [Native]::WindowFromPoint($p)
$sb = New-Object System.Text.StringBuilder 256
[void][Native]::GetWindowText($h, $sb, 256)
$pid2 = [uint32]0
[void][Native]::GetWindowThreadProcessId($h, [ref]$pid2)
$procName = (Get-Process -Id $pid2 -ErrorAction SilentlyContinue).ProcessName
$cp = New-Object Native+POINT
[void][Native]::GetCursorPos([ref]$cp)
Write-Output ("POINT {0},{1} -> hwnd={2} title='{3}' pid={4} proc={5} | cursorNow={6},{7}" -f $X, $Y, $h, $sb.ToString(), $pid2, $procName, $cp.X, $cp.Y)
$gcad = Get-Process -Name GarmentCAD -ErrorAction SilentlyContinue | Select-Object -First 1
Write-Output ("GarmentCAD MainWindowHandle={0}" -f $gcad.MainWindowHandle)

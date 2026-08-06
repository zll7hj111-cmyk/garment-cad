param([string]$Act, [int]$X=0, [int]$Y=0, [int]$Hold=80)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int n);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
  [StructLayout(LayoutKind.Sequential)]
  public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT { public uint type; public MOUSEINPUT mi; }
  [DllImport("user32.dll", SetLastError=true)]
  public static extern uint SendInput(uint n, INPUT[] p, int cb);
  public static uint Send(uint flags, int nx, int ny) {
    INPUT[] inp = new INPUT[1];
    inp[0].type = 0;
    inp[0].mi.dx = nx; inp[0].mi.dy = ny; inp[0].mi.dwFlags = flags;
    return SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
  }
  public static int Size() { return Marshal.SizeOf(typeof(INPUT)); }
}
"@
[void][Native]::SetProcessDpiAwarenessContext([Native]::PMV2)
Write-Output ("INPUT size = {0} bytes" -f [Native]::Size())
$proc = Get-Process -Name GarmentCAD -ErrorAction SilentlyContinue | Select-Object -First 1
if ($Act -ne "pos" -and $proc) { [void][Native]::SetForegroundWindow($proc.MainWindowHandle); Start-Sleep -Milliseconds 150 }
$vw=[Native]::GetSystemMetrics(78); $vh=[Native]::GetSystemMetrics(79); $vx=[Native]::GetSystemMetrics(76); $vy=[Native]::GetSystemMetrics(77)
$nx=[int]((($X-$vx)*65535.0)/($vw-1)); $ny=[int]((($Y-$vy)*65535.0)/($vh-1))
if ($Act -eq "move") {
  $r=[Native]::Send(0x8001,$nx,$ny); Start-Sleep -Milliseconds 120
  $cp=New-Object Native+POINT; [void][Native]::GetCursorPos([ref]$cp)
  Write-Output ("MOVE target={0},{1} sent={2} cursor={3},{4}" -f $X,$Y,$r,$cp.X,$cp.Y)
} elseif ($Act -eq "click") {
  $r1=[Native]::Send(0x8001,$nx,$ny); Start-Sleep -Milliseconds 100
  $r2=[Native]::Send(0x8002,$nx,$ny); Start-Sleep -Milliseconds $Hold
  $r3=[Native]::Send(0x8004,$nx,$ny)
  $cp=New-Object Native+POINT; [void][Native]::GetCursorPos([ref]$cp)
  Write-Output ("CLICK target={0},{1} sent={2},{3},{4} cursor={5},{6}" -f $X,$Y,$r1,$r2,$r3,$cp.X,$cp.Y)
} elseif ($Act -eq "pos") {
  $cp=New-Object Native+POINT; [void][Native]::GetCursorPos([ref]$cp)
  Write-Output ("CURSOR {0},{1}" -f $cp.X,$cp.Y)
}

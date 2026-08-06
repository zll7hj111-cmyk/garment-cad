param([int]$X, [int]$Y)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int nIndex);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT { public uint type; public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [DllImport("user32.dll", SetLastError=true)]
  public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
}
"@
[void][Native]::SetProcessDpiAwarenessContext([Native]::PMV2)
$proc = Get-Process -Name GarmentCAD -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc) { [void][Native]::SetForegroundWindow($proc.MainWindowHandle); Start-Sleep -Milliseconds 200 }
$vw = [Native]::GetSystemMetrics(78); $vh = [Native]::GetSystemMetrics(79)
$vx = [Native]::GetSystemMetrics(76); $vy = [Native]::GetSystemMetrics(77)
$nx = [int]((($X - $vx) * 65535.0) / ($vw - 1))
$ny = [int]((($Y - $vy) * 65535.0) / ($vh - 1))
$inp = New-Object Native+INPUT[] 4
$inp[0].type = 0; $inp[0].dx = $nx; $inp[0].dy = $ny; $inp[0].dwFlags = 0x8001
$inp[1].type = 0; $inp[1].dx = $nx; $inp[1].dy = $ny; $inp[1].dwFlags = 0x8002
$inp[2].type = 0; $inp[2].dx = $nx; $inp[2].dy = $ny; $inp[2].dwFlags = 0x8004
$inp[3].type = 0; $inp[3].dx = $nx; $inp[3].dy = $ny; $inp[3].dwFlags = 0x8001
$sent = [Native]::SendInput(4, $inp, [System.Runtime.InteropServices.Marshal]::SizeOf([type][Native+INPUT]))
$cp = New-Object Native+POINT
[void][Native]::GetCursorPos([ref]$cp)
Write-Output ("ATOMICCLICK target={0},{1} sent={2} cursorAfter={3},{4}" -f $X, $Y, $sent, $cp.X, $cp.Y)

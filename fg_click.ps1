param([int]$X, [int]$Y)
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class FG {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
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
$proc = Get-Process -Name GarmentCAD -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc) {
  [void][FG]::ShowWindow($proc.MainWindowHandle, 9)
  [void][FG]::SetForegroundWindow($proc.MainWindowHandle)
  Start-Sleep -Milliseconds 300
}
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
$nx = [int](($X * 65535.0) / ($vs.Width - 1))
$ny = [int](($Y * 65535.0) / ($vs.Height - 1))
[FG]::Send(0x8001, $nx, $ny)
Start-Sleep -Milliseconds 150
[FG]::Send(0x8003, $nx, $ny)
Start-Sleep -Milliseconds 60
[FG]::Send(0x8005, $nx, $ny)
$fg = [FG]::GetForegroundWindow()
Write-Output ("CLICKED {0},{1} fgHandle={2} targetHandle={3}" -f $X, $Y, $fg, $proc.MainWindowHandle)

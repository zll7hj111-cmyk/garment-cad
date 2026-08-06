param(
  [Parameter(Mandatory=$true)][string]$Action,
  [int]$X = 0,
  [int]$Y = 0,
  [int]$Steps = 1,
  [int]$DelayMs = 0
)
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class MouseOps {
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT {
    public uint type;
    public MOUSEINPUT mi;
    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT {
      public int dx; public int dy;
      public uint mouseData; public uint dwFlags;
      public uint time; public IntPtr dwExtraInfo;
    }
  }
  [DllImport("user32.dll", SetLastError=true)]
  public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
  public const uint INPUT_MOUSE = 0;
  public const uint MOUSEEVENTF_MOVE = 0x0001;
  public const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
  public const uint MOUSEEVENTF_LEFTUP = 0x0004;
  public const uint MOUSEEVENTF_ABSOLUTE = 0x8000;
  public static void Send(uint flags, int nx, int ny) {
    INPUT[] inp = new INPUT[1];
    inp[0].type = INPUT_MOUSE;
    inp[0].mi.dx = nx; inp[0].mi.dy = ny;
    inp[0].mi.dwFlags = flags;
    SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
$nx = [int](($X * 65535.0) / ($vs.Width - 1))
$ny = [int](($Y * 65535.0) / ($vs.Height - 1))
if ($Action -eq "click") {
  [MouseOps]::Send([MouseOps]::MOUSEEVENTF_MOVE -bor [MouseOps]::MOUSEEVENTF_ABSOLUTE, $nx, $ny)
  Start-Sleep -Milliseconds 150
  [MouseOps]::Send([MouseOps]::MOUSEEVENTF_LEFTDOWN -bor [MouseOps]::MOUSEEVENTF_ABSOLUTE, $nx, $ny)
  Start-Sleep -Milliseconds 60
  [MouseOps]::Send([MouseOps]::MOUSEEVENTF_LEFTUP -bor [MouseOps]::MOUSEEVENTF_ABSOLUTE, $nx, $ny)
  Write-Output ("CLICKED at {0},{1}" -f $X, $Y)
} elseif ($Action -eq "move") {
  [MouseOps]::Send([MouseOps]::MOUSEEVENTF_MOVE -bor [MouseOps]::MOUSEEVENTF_ABSOLUTE, $nx, $ny)
  Write-Output ("MOVED to {0},{1}" -f $X, $Y)
} elseif ($Action -eq "slowmove") {
  $cursor = [System.Windows.Forms.Cursor]::Position
  $sx = $cursor.X; $sy = $cursor.Y
  for ($i = 1; $i -le $Steps; $i++) {
    $cx = [int]($sx + ($X - $sx) * $i / $Steps)
    $cy = [int]($sy + ($Y - $sy) * $i / $Steps)
    $cnx = [int](($cx * 65535.0) / ($vs.Width - 1))
    $cny = [int](($cy * 65535.0) / ($vs.Height - 1))
    [MouseOps]::Send([MouseOps]::MOUSEEVENTF_MOVE -bor [MouseOps]::MOUSEEVENTF_ABSOLUTE, $cnx, $cny)
    Start-Sleep -Milliseconds $DelayMs
  }
  $final = [System.Windows.Forms.Cursor]::Position
  Write-Output ("SLOWMOVED to {0},{1} final={2},{3}" -f $X, $Y, $final.X, $final.Y)
} elseif ($Action -eq "pos") {
  $p = [System.Windows.Forms.Cursor]::Position
  Write-Output ("CURSOR at {0},{1}" -f $p.X, $p.Y)
}

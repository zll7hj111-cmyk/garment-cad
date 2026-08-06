param(
  [Parameter(Mandatory=$true)][string]$Name,
  [double]$DX = 0.5,
  [double]$DY = 0.5
)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  public static readonly IntPtr DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = new IntPtr(-4);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int nIndex);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
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
[void][Native]::SetProcessDpiAwarenessContext([Native]::DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$proc = Get-Process -Name GarmentCAD -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc) { [void][Native]::SetForegroundWindow($proc.MainWindowHandle); Start-Sleep -Milliseconds 200 }
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $Name)
$el = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
if (-not $el) { Write-Output ("NOTFOUND {0}" -f $Name); exit 1 }
$r = $el.Current.BoundingRectangle
$cx = [int]($r.X + $r.Width * $DX)
$cy = [int]($r.Y + $r.Height * $DY)
$vw = [Native]::GetSystemMetrics(78)
$vh = [Native]::GetSystemMetrics(79)
$vx = [Native]::GetSystemMetrics(76)
$vy = [Native]::GetSystemMetrics(77)
$nx = [int]((($cx - $vx) * 65535.0) / ($vw - 1))
$ny = [int]((($cy - $vy) * 65535.0) / ($vh - 1))
[Native]::Send(0x8001, $nx, $ny)
Start-Sleep -Milliseconds 150
[Native]::Send(0x8003, $nx, $ny)
Start-Sleep -Milliseconds 60
[Native]::Send(0x8005, $nx, $ny)
Write-Output ("CLICKED name={0} rect={1},{2},{3},{4} center={5},{6} vscreen={7},{8},{9},{10}" -f $Name, $r.X, $r.Y, $r.Width, $r.Height, $cx, $cy, $vx, $vy, $vw, $vh)

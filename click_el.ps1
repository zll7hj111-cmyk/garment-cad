param([string]$Name, [string]$Aid, [double]$DX=0.5, [double]$DY=0.5, [switch]$NoFG)
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
    inp[0].type = 0; inp[0].mi.dx = nx; inp[0].mi.dy = ny; inp[0].mi.dwFlags = flags;
    return SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
[void][Native]::SetProcessDpiAwarenessContext([Native]::PMV2)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
if (-not $NoFG) {
  $proc = Get-Process -Name GarmentCAD -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($proc) { [void][Native]::SetForegroundWindow($proc.MainWindowHandle); Start-Sleep -Milliseconds 150 }
}
$el = $null
if ($Aid) {
  $c = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $Aid)
  $el = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $c)
} elseif ($Name) {
  $c = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $Name)
  $el = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $c)
}
if (-not $el) { Write-Output "NOTFOUND"; exit 1 }
$r = $el.Current.BoundingRectangle
$cx = [int]($r.X + $r.Width*$DX); $cy = [int]($r.Y + $r.Height*$DY)
$vw=[Native]::GetSystemMetrics(78); $vh=[Native]::GetSystemMetrics(79); $vx=[Native]::GetSystemMetrics(76); $vy=[Native]::GetSystemMetrics(77)
$nx=[int]((($cx-$vx)*65535.0)/($vw-1)); $ny=[int]((($cy-$vy)*65535.0)/($vh-1))
$r1=[Native]::Send(0x8001,$nx,$ny); Start-Sleep -Milliseconds 100
$r2=[Native]::Send(0x8002,$nx,$ny); Start-Sleep -Milliseconds 70
$r3=[Native]::Send(0x8004,$nx,$ny)
$cp=New-Object Native+POINT; [void][Native]::GetCursorPos([ref]$cp)
Write-Output ("CLICKED name='{0}' center={1},{2} sent={3},{4},{5} cursor={6},{7}" -f $el.Current.Name,$cx,$cy,$r1,$r2,$r3,$cp.X,$cp.Y)

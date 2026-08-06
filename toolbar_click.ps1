param([int]$Index)
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
$proc = Get-Process -Name GarmentCAD -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc) { [void][Native]::SetForegroundWindow($proc.MainWindowHandle); Start-Sleep -Milliseconds 150 }
$tbCond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, "QApplication.MainWindow.QToolBar")
$tb = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $tbCond)
if (-not $tb) { Write-Output "TOOLBAR NOTFOUND"; exit 1 }
$btns = $tb.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
if ($Index -ge $btns.Count) { Write-Output "INDEX OUT OF RANGE"; exit 1 }
$el = $btns[$Index]
$r = $el.Current.BoundingRectangle
$cx=[int]($r.X+$r.Width/2); $cy=[int]($r.Y+$r.Height/2)
$vw=[Native]::GetSystemMetrics(78); $vh=[Native]::GetSystemMetrics(79); $vx=[Native]::GetSystemMetrics(76); $vy=[Native]::GetSystemMetrics(77)
$nx=[int]((($cx-$vx)*65535.0)/($vw-1)); $ny=[int]((($cy-$vy)*65535.0)/($vh-1))
$r1=[Native]::Send(0x8001,$nx,$ny); Start-Sleep -Milliseconds 100
$r2=[Native]::Send(0x8002,$nx,$ny); Start-Sleep -Milliseconds 70
$r3=[Native]::Send(0x8004,$nx,$ny)
Write-Output ("TOOLCLICK idx={0} name='{1}' center={2},{3} sent={4},{5},{6}" -f $Index,$el.Current.Name,$cx,$cy,$r1,$r2,$r3)

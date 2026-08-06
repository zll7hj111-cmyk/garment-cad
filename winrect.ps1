Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
}
"@
[void][Native]::SetProcessDpiAwarenessContext([Native]::PMV2)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$proc = Get-Process -Name GarmentCAD | Select-Object -First 1
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $proc.Id)
$wins = $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)
foreach ($w in $wins) {
  $r = $w.Current.BoundingRectangle
  Write-Output ("WINDOW '{0}' rect={1},{2},{3},{4}" -f $w.Current.Name, [int]$r.X,[int]$r.Y,[int]$r.Width,[int]$r.Height)
}
$canvasCond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, "QApplication.MainWindow.CanvasView")
$cv = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $canvasCond)
if ($cv) { $cr = $cv.Current.BoundingRectangle; Write-Output ("CANVAS rect={0},{1},{2},{3}" -f [int]$cr.X,[int]$cr.Y,[int]$cr.Width,[int]$cr.Height) }

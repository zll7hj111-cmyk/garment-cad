Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
}
"@
[void][Native]::SetProcessDpiAwarenessContext([Native]::PMV2)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$treeCond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, "QApplication.MainWindow.SidePanel.QWidget.QStackedWidget.LayerPanel.QTreeWidget")
$tree = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $treeCond)
if ($tree) {
  $items = $tree.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
  foreach ($it in $items) {
    $r = $it.Current.BoundingRectangle
    Write-Output ("TREEITEM name='{0}' rect={1},{2},{3},{4}" -f $it.Current.Name, [int]$r.X, [int]$r.Y, [int]$r.Width, [int]$r.Height)
  }
}
$btnCond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, "QApplication.MainWindow.SidePanel.QWidget.QStackedWidget.LayerPanel.QToolButton")
$btn = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $btnCond)
if ($btn) { $br = $btn.Current.BoundingRectangle; Write-Output ("PLUSBTN rect={0},{1},{2},{3}" -f [int]$br.X,[int]$br.Y,[int]$br.Width,[int]$br.Height) }
$cp1 = New-Object Native+POINT; [void][Native]::GetCursorPos([ref]$cp1)
Start-Sleep -Milliseconds 800
$cp2 = New-Object Native+POINT; [void][Native]::GetCursorPos([ref]$cp2)
Write-Output ("CURSOR t0={0},{1} t1={2},{3}" -f $cp1.X,$cp1.Y,$cp2.X,$cp2.Y)

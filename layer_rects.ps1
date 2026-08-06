Add-Type @"
using System;
using System.Runtime.InteropServices;
public class N2 {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
}
"@
[void][N2]::SetProcessDpiAwarenessContext([N2]::PMV2)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root=[System.Windows.Automation.AutomationElement]::RootElement
$c=New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,"QApplication.MainWindow.CanvasView")
$cv=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$c)
if($cv){$r=$cv.Current.BoundingRectangle;Write-Output ("CANVAS rect=(" + [int]$r.X + "," + [int]$r.Y + "," + [int]$r.Width + "," + [int]$r.Height + ")")}
$c2=New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,"QApplication.MainWindow.SidePanel.QWidget.QStackedWidget.LayerPanel.QTreeWidget")
$tree=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$c2)
if(-not $tree){Write-Output "TREENOTFOUND";exit}
$items=$tree.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)
foreach($it in $items){
$r=$it.Current.BoundingRectangle
if($r.Width -gt 0){
Write-Output ("name=[" + $it.Current.Name + "] rect=(" + [int]$r.X + "," + [int]$r.Y + "," + [int]$r.Width + "," + [int]$r.Height + ")")
}
}


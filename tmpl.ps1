Add-Type @QQ
using System;
using System.Runtime.InteropServices;
public class N2 {
  [DllImport(QQuser32.dllQQ)] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
}
QQ@
[void][N2]::SetProcessDpiAwarenessContext([N2]::PMV2)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root=[System.Windows.Automation.AutomationElement]::RootElement
$c=New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,QQQApplication.MainWindow.CanvasViewQQ)
$cv=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$c)
if($cv){$r=$cv.Current.BoundingRectangle;Write-Output (QQCANVAS rect=(QQ + [int]$r.X + QQ,QQ + [int]$r.Y + QQ,QQ + [int]$r.Width + QQ,QQ + [int]$r.Height + QQ)QQ)}
$c2=New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,QQQApplication.MainWindow.SidePanel.QWidget.QStackedWidget.LayerPanel.QTreeWidgetQQ)
$tree=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$c2)
if(-not $tree){Write-Output QQTREENOTFOUNDQQ;exit}
$items=$tree.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)
foreach($it in $items){
$r=$it.Current.BoundingRectangle
if($r.Width -gt 0){
Write-Output (QQname=[QQ + $it.Current.Name + QQ] rect=(QQ + [int]$r.X + QQ,QQ + [int]$r.Y + QQ,QQ + [int]$r.Width + QQ,QQ + [int]$r.Height + QQ)QQ)
}
}

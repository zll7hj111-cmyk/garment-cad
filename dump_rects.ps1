Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  public static readonly IntPtr PMV2 = new IntPtr(-4);
}
"@
[void][Native]::SetProcessDpiAwarenessContext([Native]::PMV2)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$names = @('选择(V)','智能笔(L)','旋转(R)','变量','图层','添加','关闭','面板')
foreach ($n in $names) {
  $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $n)
  $els = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)
  foreach ($el in $els) {
    $r = $el.Current.BoundingRectangle
    $t = $el.Current.AutomationId
    Write-Output ("{0} | rect={1},{2},{3},{4} | id={5}" -f $n, [int]$r.X, [int]$r.Y, [int]$r.Width, [int]$r.Height, $t)
  }
}

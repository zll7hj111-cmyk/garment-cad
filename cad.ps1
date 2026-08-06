param([string]$Cmd='rect',[string]$Arg1='',[double]$RX=0,[double]$RY=0)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$q = [char]34
$sig = 'using System;
using System.Runtime.InteropServices;
public static class MO {
  [DllImport(' + $q + 'user32.dll' + $q + ')] public static extern bool SetCursorPos(int X, int Y);
  [DllImport(' + $q + 'user32.dll' + $q + ')] public static extern void mouse_event(uint f, uint dx, uint dy, uint dw, int ex);
  public static void Click(int x, int y) {
    SetCursorPos(x, y);
    System.Threading.Thread.Sleep(300);
    mouse_event(2, 0, 0, 0, 0);
    System.Threading.Thread.Sleep(80);
    mouse_event(4, 0, 0, 0, 0);
    System.Threading.Thread.Sleep(150);
  }
}'
Add-Type -TypeDefinition $sig
$root = [System.Windows.Automation.AutomationElement]::RootElement
$all = $root.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
$match = -join @([char]0x670D,[char]0x88C5,'CAD')
$win = $null
foreach ($w in $all) {
  $nm = $w.Current.Name
  if ($nm -ne $null -and $nm.Contains($match)) { $win = $w; break }
}
if ($win -eq $null) { Write-Output 'WIN NOT FOUND'; exit 1 }
Write-Output ('WINDOW ' + $win.Current.Name)
$cN = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $Arg1)
$el = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cN)
if ($Cmd -eq 'rect') {
  if ($el -eq $null) { Write-Output 'NOT FOUND' } else { Write-Output ($el.Current.BoundingRectangle.ToString()) }
  exit 0
}
if ($Cmd -eq 'clickname') {
  if ($el -eq $null) { Write-Output 'NOT FOUND'; exit 1 }
  $r = $el.Current.BoundingRectangle
  $cx = [int]($r.X + $r.Width/2); $cy = [int]($r.Y + $r.Height/2)
  Write-Output ('CLICKAT ' + $cx + ',' + $cy)
  [MO]::Click($cx, $cy)
  exit 0
}
if ($Cmd -eq 'clickid') {
  $cI = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $Arg1)
  $el2 = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cI)
  if ($el2 -eq $null) { Write-Output 'NOT FOUND'; exit 1 }
  $r = $el2.Current.BoundingRectangle
  $cx = [int]($r.X + $RX*$r.Width); $cy = [int]($r.Y + $RY*$r.Height)
  Write-Output ('CLICKAT ' + $cx + ',' + $cy)
  [MO]::Click($cx, $cy)
  exit 0
}

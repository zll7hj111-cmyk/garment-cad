Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$all = $root.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
$match = -join @([char]0x670D,[char]0x88C5,'CAD')
$win = $null
foreach ($w in $all) {
  $nm = $w.Current.Name
  if ($nm -ne $null -and $nm.Contains($match)) { $win = $w; break }
}
if ($win -eq $null) { Write-Output 'WIN NOT FOUND'; exit 1 }
$cI = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'QApplication.MainWindow.SidePanel.QWidget.QStackedWidget.LayerPanel.QTreeWidget')
$tree = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cI)
if ($tree -eq $null) { Write-Output 'TREE NOT FOUND'; exit 1 }
Write-Output ('TREE ' + $tree.Current.BoundingRectangle.ToString())
$kids = $tree.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
$i = 0
foreach ($k in $kids) {
  $r = $k.Current.BoundingRectangle
  $n = $k.Current.Name
  Write-Output ('ITEM[' + $i + '] name=[' + $n + '] rect=' + $r.ToString())
  $i++
}

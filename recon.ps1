Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Windows.Forms
Write-Output ('SCREEN ' + [System.Windows.Forms.Screen]::PrimaryScreen.Bounds.ToString())
$wname = -join @([char]0x672A,[char]0x547D,[char]0x540D,' - ',[char]0x670D,[char]0x88C5,'CAD')
$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $wname)
$win = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
Write-Output ('WINRECT ' + $win.Current.BoundingRectangle.ToString())
$names = @((-join @([char]0x56FE,[char]0x5C42)),(-join @([char]0x53D8,[char]0x91CF)),(-join @([char]0x667A,[char]0x80FD,[char]0x7B14,'(L)')),(-join @([char]0x9009,[char]0x62E9,'(V)')),(-join @([char]0x6807,[char]0x9898,[char]0x680F)))
foreach ($n in $names) {
  $tc = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $n)
  $el = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $tc)
  if ($el -eq $null) { Write-Output ($n + ' NOTFOUND') } else { Write-Output ($n + ' ' + $el.Current.BoundingRectangle.ToString()) }
}
$idc = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'QApplication.MainWindow.CanvasView')
$cv = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $idc)
if ($cv -eq $null) { Write-Output 'CANVAS NOTFOUND' } else { Write-Output ('CANVAS ' + $cv.Current.BoundingRectangle.ToString()) }

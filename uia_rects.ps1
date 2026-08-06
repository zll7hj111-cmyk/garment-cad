Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$wname = -join @([char]0x672A,[char]0x547D,[char]0x540D,' - ',[char]0x670D,[char]0x88C5,'CAD')
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $wname)
$win = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
Write-Output ('WIN ' + $win.Current.BoundingRectangle.ToString())
$names = @((-join @([char]0x56FE,[char]0x5C42)), (-join @([char]0x667A,[char]0x80FD,[char]0x7B14,'(L)')), (-join @([char]0x9009,[char]0x62E9,'(V)')))
foreach ($n in $names) {
  $tc = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $n)
  $el = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $tc)
  if ($el -ne $null) { Write-Output ($n + ' ' + $el.Current.BoundingRectangle.ToString()) } else { Write-Output ($n + ' NOTFOUND') }
}

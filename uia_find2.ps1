Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$nameCond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, [string]::Concat([char]0x672A, [char]0x547D, [char]0x540D, ' - ', [char]0x670D, [char]0x88C5, 'CAD'))
$win = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $nameCond)
if ($win) {
  $wr = $win.Current.BoundingRectangle
  Write-Output ("WINDOW x={0} y={1} w={2} h={3}" -f $wr.X, $wr.Y, $wr.Width, $wr.Height)
  $tabCond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, [string]::Concat([char]0x53D8, [char]0x91CF))
  $tab = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $tabCond)
  if ($tab) {
    $tr = $tab.Current.BoundingRectangle
    Write-Output ("BIANLIANG x={0} y={1} w={2} h={3}" -f $tr.X, $tr.Y, $tr.Width, $tr.Height)
  }
  $addCond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, [string]::Concat([char]0x6DFB, [char]0x52A0))
  $addBtn = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $addCond)
  if ($addBtn) {
    $ar = $addBtn.Current.BoundingRectangle
    Write-Output ("ADD x={0} y={1} w={2} h={3}" -f $ar.X, $ar.Y, $ar.Width, $ar.Height)
  }
} else {
  Write-Output "WINDOW NOT FOUND"
}

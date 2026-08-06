Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, [string]::Concat([char]0x56FE, [char]0x5C42))
$el = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
if ($el) {
  $r = $el.Current.BoundingRectangle
  Write-Output ("FOUND x={0} y={1} w={2} h={3}" -f $r.X, $r.Y, $r.Width, $r.Height)
} else {
  Write-Output "NOT FOUND"
}

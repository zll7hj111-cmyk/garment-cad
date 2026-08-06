Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class DpiHelper {
  [DllImport("user32.dll")] public static extern int GetDpiForSystem();
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
$dpi = [DpiHelper]::GetDpiForSystem()
Write-Output ("SystemDPI={0} Scale={1}" -f $dpi, ($dpi/96.0))
$screen = [System.Windows.Forms.SystemInformation]::PrimaryMonitorSize
Write-Output ("PrimaryMonitorSize={0}x{1}" -f $screen.Width, $screen.Height)
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
Write-Output ("VirtualScreen={0},{1} {2}x{3}" -f $vs.X, $vs.Y, $vs.Width, $vs.Height)

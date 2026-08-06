Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
}
"@
for ($i = 0; $i -lt 6; $i++) {
  $cp = New-Object Native+POINT
  [void][Native]::GetCursorPos([ref]$cp)
  Write-Output ("sample {0}: {1},{2}" -f $i, $cp.X, $cp.Y)
  Start-Sleep -Milliseconds 400
}

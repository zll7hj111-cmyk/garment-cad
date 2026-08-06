Add-Type -AssemblyName System.Drawing
$path = $args[0]
$img = [System.Drawing.Image]::FromFile($path)
Write-Host "Size: $($img.Width)x$($img.Height)"
# Canvas region in image pixels: exclude toolbar(top ~150px) and statusbar(bottom ~60px), right panel(~1250+)
$pts = New-Object System.Collections.ArrayList
for ($y = 160; $y -lt 990; $y++) {
  for ($x = 40; $x -lt 1240; $x++) {
    $p = $img.GetPixel($x, $y)
    $mx = [Math]::Max($p.R, [Math]::Max($p.G, $p.B))
    $mn = [Math]::Min($p.R, [Math]::Min($p.G, $p.B))
    # colored pixel: saturation diff > 50, and not blue-ish
    if (($mx - $mn) -gt 50 -and $p.R -gt $p.B) {
      [void]$pts.Add(@($x, $y, $p.R, $p.G, $p.B))
    }
  }
}
$img.Dispose()
Write-Host "Colored pixels in canvas: $($pts.Count)"
if ($pts.Count -gt 0) {
  $minX = ($pts | ForEach-Object { $_[0] } | Measure-Object -Minimum).Minimum
  $maxX = ($pts | ForEach-Object { $_[0] } | Measure-Object -Maximum).Maximum
  $minY = ($pts | ForEach-Object { $_[1] } | Measure-Object -Minimum).Minimum
  $maxY = ($pts | ForEach-Object { $_[1] } | Measure-Object -Maximum).Maximum
  Write-Host "BBox: minX=$minX maxX=$maxX minY=$minY maxY=$maxY"
  Write-Host "Center: $([int](($minX+$maxX)/2)),$([int](($minY+$maxY)/2))"
  $pts | Select-Object -First 12 | ForEach-Object { Write-Host "$($_[0]),$($_[1])=($($_[2]),$($_[3]),$($_[4]))" }
}

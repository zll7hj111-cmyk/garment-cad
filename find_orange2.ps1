Add-Type -AssemblyName System.Drawing
$path = $args[0]
$img = [System.Drawing.Image]::FromFile($path)
Write-Host "Size: $($img.Width)x$($img.Height)"
$pts = New-Object System.Collections.ArrayList
for ($y = 0; $y -lt $img.Height; $y++) {
  for ($x = 0; $x -lt $img.Width; $x++) {
    $p = $img.GetPixel($x, $y)
    if ($p.R -gt 190 -and $p.G -gt 90 -and $p.G -lt 200 -and $p.B -lt 90) {
      [void]$pts.Add(@($x, $y))
    }
  }
}
$img.Dispose()
Write-Host "Total orange pixels: $($pts.Count)"
# Simple grid histogram (50px cells) to find clusters
$hist = @{}
foreach ($pt in $pts) {
  $cx = [int]($pt[0] / 50); $cy = [int]($pt[1] / 50)
  $key = "$cx,$cy"
  if (-not $hist.ContainsKey($key)) { $hist[$key] = 0 }
  $hist[$key]++
}
Write-Host "--- Grid histogram (cell=50px, count>=3) ---"
$hist.GetEnumerator() | Where-Object { $_.Value -ge 3 } | Sort-Object { [int]($_.Key.Split(',')[0]) + [int]($_.Key.Split(',')[1]) * 100 } | ForEach-Object {
  $k = $_.Key.Split(',')
  Write-Host ("cell({0},{1}) px-range({2}-{3},{4}-{5}) count={6}" -f $k[0], $k[1], ([int]$k[0]*50), ([int]$k[0]*50+49), ([int]$k[1]*50), ([int]$k[1]*50+49), $_.Value)
}

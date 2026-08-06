Add-Type -AssemblyName System.Drawing
$path = $args[0]
$img = [System.Drawing.Image]::FromFile($path)
Write-Host "Size: $($img.Width)x$($img.Height)"
$minX = [int]::MaxValue; $maxX = [int]::MinValue; $minY = [int]::MaxValue; $maxY = [int]::MinValue
$count = 0
$sample = @()
for ($y = 0; $y -lt $img.Height; $y++) {
  for ($x = 0; $x -lt $img.Width; $x++) {
    $p = $img.GetPixel($x, $y)
    if ($p.R -gt 190 -and $p.G -gt 90 -and $p.G -lt 200 -and $p.B -lt 90) {
      $count++
      if ($x -lt $minX) { $minX = $x }
      if ($x -gt $maxX) { $maxX = $x }
      if ($y -lt $minY) { $minY = $y }
      if ($y -gt $maxY) { $maxY = $y }
      if ($sample.Count -lt 8) { $sample += "$x,$y=($($p.R),$($p.G),$($p.B))" }
    }
  }
}
$img.Dispose()
if ($count -eq 0) { Write-Host "NO ORANGE FOUND" }
else {
  Write-Host "Count: $count"
  Write-Host "BBox: minX=$minX maxX=$maxX minY=$minY maxY=$maxY"
  Write-Host "Center: $([int](($minX+$maxX)/2)),$([int](($minY+$maxY)/2))"
  $sample | ForEach-Object { Write-Host $_ }
}

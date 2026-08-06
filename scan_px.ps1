Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$x0 = 1683; $y0 = 470; $w = 40; $h = 1
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($x0, $y0, 0, 0, (New-Object System.Drawing.Size($w, $h)))
$g.Dispose()
$sb = ''
for ($i = 0; $i -lt $w; $i++) {
  $c = $bmp.GetPixel($i, 0)
  $sb += ('{0}:{1},{2},{3} ' -f $i, $c.R, $c.G, $c.B)
}
Write-Output $sb
$bmp.Dispose()

Add-Type -AssemblyName System.Drawing
 = [System.Drawing.Image]::FromFile('C:\Users\Administrator\AppData\Local\Temp\qoder-computer-use-images\7dcb8b7a\img-1785516958635369200-966397.png')
 = New-Object System.Drawing.Rectangle(340,190,440,280)
 = New-Object System.Drawing.Bitmap(1320,840)
 = [System.Drawing.Graphics]::FromImage()
.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
.DrawImage(, (New-Object System.Drawing.Rectangle(0,0,1320,840)), , [System.Drawing.GraphicsUnit]::Pixel)
.Dispose()
.Save('e:\garment-cad\curve-zoom.png', [System.Drawing.Imaging.ImageFormat]::Png)
.Dispose()
.Dispose()
Write-Host done

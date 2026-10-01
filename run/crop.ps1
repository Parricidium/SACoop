# Recadre une capture de fenetre (sans barre de titre ni bordures) en JPEG : run\crop.ps1 source.png sortie.jpg
param([string]$Src, [string]$Dst, [int]$Quality = 88)
Add-Type -AssemblyName System.Drawing
$enc = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
$p = New-Object System.Drawing.Imaging.EncoderParameters 1
$p.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality, [long]$Quality)
$im = [System.Drawing.Image]::FromFile((Resolve-Path $Src))
$r = New-Object System.Drawing.Rectangle 3, 37, ($im.Width - 6), ($im.Height - 40)
$b = New-Object System.Drawing.Bitmap $r.Width, $r.Height
$g = [System.Drawing.Graphics]::FromImage($b)
$g.DrawImage($im, (New-Object System.Drawing.Rectangle 0, 0, $r.Width, $r.Height), $r, [System.Drawing.GraphicsUnit]::Pixel)
$b.Save($Dst, $enc, $p)
$im.Dispose(); $b.Dispose()
"$Dst : $($r.Width)x$($r.Height)"

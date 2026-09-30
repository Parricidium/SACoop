# Compose les fonds du lanceur (dist\files\SACoop\interface\launcher.png et launcher-sombre.png, 1000x620, avec transparence)
# et son icone (launcher\sacoop.ico) : carte arrondie + ombre douce, coucher de soleil sur Los Santos (dessine ici : ciel,
# soleil, collines de Vinewood, immeubles, palmiers), panneau depoli a gauche, logo qui depasse de la carte.
# Le lanceur dessine ses textes et boutons par-dessus (coordonnees fixes, voir launcher.cpp).
# Logo : launcher\logo.png (celui de JD) ; tant qu'il n'est pas la, un logo provisoire (texte) est dessine.
Add-Type -AssemblyName System.Drawing
$ui = Join-Path (Split-Path $PSScriptRoot) 'dist\files\SACoop\interface'
New-Item -ItemType Directory -Force $ui | Out-Null
$W = 1000; $H = 620
$S = 2   # fonds en double resolution (ecrans a 150-200 %) ; le lanceur les dessine en 1000x620
$card = New-Object System.Drawing.RectangleF 20, 60, 960, 540
$panel = New-Object System.Drawing.RectangleF 48, 88, 360, 500

function C($a, $r, $g, $b) { [System.Drawing.Color]::FromArgb($a, $r, $g, $b) }

function AlphaBox($img) {
    $b = New-Object System.Drawing.Bitmap $img
    $x0 = $b.Width; $y0 = $b.Height; $x1 = -1; $y1 = -1
    for ($y = 0; $y -lt $b.Height; $y += 2) { for ($x = 0; $x -lt $b.Width; $x += 2) {
        if ($b.GetPixel($x, $y).A -gt 8) { if ($x -lt $x0) { $x0 = $x }; if ($x -gt $x1) { $x1 = $x }; if ($y -lt $y0) { $y0 = $y }; if ($y -gt $y1) { $y1 = $y } }
    } }
    $b.Dispose()
    return New-Object System.Drawing.RectangleF ($x0 - 2), ($y0 - 2), ($x1 - $x0 + 5), ($y1 - $y0 + 5)
}

function RoundPath([System.Drawing.RectangleF]$r, [float]$rad) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $rad * 2
    $p.AddArc($r.X, $r.Y, $d, $d, 180, 90)
    $p.AddArc($r.Right - $d, $r.Y, $d, $d, 270, 90)
    $p.AddArc($r.Right - $d, $r.Bottom - $d, $d, $d, 0, 90)
    $p.AddArc($r.X, $r.Bottom - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

# --- Logo provisoire (texte "SA COOP" vert et or, contour sombre), si launcher\logo.png manque ---
$logoFile = Join-Path $PSScriptRoot 'logo.png'
if (-not (Test-Path $logoFile)) {
    $lb = New-Object System.Drawing.Bitmap 600, 520, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $lg = [System.Drawing.Graphics]::FromImage($lb)
    $lg.SmoothingMode = 'AntiAlias'; $lg.Clear([System.Drawing.Color]::Transparent)
    $fam = New-Object System.Drawing.FontFamily 'Arial Black'
    $sf = New-Object System.Drawing.StringFormat; $sf.Alignment = 'Center'
    foreach ($t in @(@('SA', 250, 10), @('COOP', 150, 290))) {
        $p = New-Object System.Drawing.Drawing2D.GraphicsPath
        $p.AddString($t[0], $fam, 0, $t[1], (New-Object System.Drawing.RectangleF 0, $t[2], 600, 300), $sf)
        $lg.DrawPath((New-Object System.Drawing.Pen (C 255 18 24 16), 22), $p)
        $gr = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 0, $t[2]), (New-Object System.Drawing.PointF 0, ($t[2] + $t[1] * 1.1)), (C 255 64 200 96), (C 255 250 190 50)
        $lg.FillPath($gr, $p)
    }
    $lg.Dispose()
    $logoFile = Join-Path $PSScriptRoot 'logo-provisoire.png'
    $lb.Save($logoFile, [System.Drawing.Imaging.ImageFormat]::Png); $lb.Dispose()
    "logo provisoire : $logoFile (deposer le vrai dans launcher\logo.png puis relancer ce script)"
}

# --- Paysage : coucher de soleil sur Los Santos, dessine dans la carte ---
function DrawCity($g, [bool]$dark) {
    $sky = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 0, 60), (New-Object System.Drawing.PointF 0, 470), (C 255 70 60 120), (C 255 255 150 70)
    $blend = New-Object System.Drawing.Drawing2D.ColorBlend 4
    $blend.Colors = @((C 255 58 52 110), (C 255 170 80 110), (C 255 250 140 70), (C 255 255 205 120))
    $blend.Positions = @(0.0, 0.45, 0.8, 1.0)
    $sky.InterpolationColors = $blend
    $g.FillRectangle($sky, 20, 60, 960, 420)
    # soleil bas, halo
    for ($i = 10; $i -ge 1; $i--) {
        $r = 34 + $i * 14
        $g.FillEllipse((New-Object System.Drawing.SolidBrush (C ([int](14 - $i)) 255 220 150)), 700 - $r, 420 - $r, 2 * $r, 2 * $r)
    }
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (C 255 255 226 150)), 666, 386, 68, 68)
    # collines de Vinewood au loin
    $hill = New-Object System.Drawing.Drawing2D.GraphicsPath
    $pts = @(); for ($x = 20; $x -le 980; $x += 20) { $pts += New-Object System.Drawing.PointF $x, (360 + 26 * [math]::Sin($x / 70.0) + 14 * [math]::Sin($x / 23.0)) }
    $pts += New-Object System.Drawing.PointF 980, 480; $pts += New-Object System.Drawing.PointF 20, 480
    $hill.AddPolygon($pts)
    $g.FillPath((New-Object System.Drawing.SolidBrush (C 255 150 70 90)), $hill)
    # immeubles du centre-ville (graine fixe : meme image a chaque fois)
    $rnd = New-Object System.Random 1992
    $cityBrush = New-Object System.Drawing.SolidBrush (C 255 70 36 62)
    $x = 470
    while ($x -lt 980) {
        $w = $rnd.Next(18, 44); $h = $rnd.Next(40, 150)
        if ($x -gt 610 -and $x -lt 700) { $h += 60 }
        $g.FillRectangle($cityBrush, $x, 440 - $h, $w, $h + 40)
        for ($k = 0; $k -lt 6; $k++) {   # fenetres allumees
            $g.FillRectangle((New-Object System.Drawing.SolidBrush (C 150 255 200 120)), $x + $rnd.Next(3, $w - 5), 440 - $h + $rnd.Next(6, $h), 3, 4)
        }
        $x += $w + $rnd.Next(0, 6)
    }
    # sol et route
    $g.FillRectangle((New-Object System.Drawing.SolidBrush (C 255 44 22 40)), 20, 440, 960, 160)
    # palmiers en ombre chinoise : tronc courbe, palmes en arc (feuille pleine : deux courbes de Bezier)
    $ink = New-Object System.Drawing.SolidBrush (C 255 30 14 28)
    foreach ($pt in @(@(905, 1.1, 1), @(958, 0.85, -1), @(452, 0.9, 1))) {
        $px = $pt[0]; $k = $pt[1]; $lean = $pt[2]
        $trunk = New-Object System.Drawing.Pen (C 255 30 14 28), (6 * $k)
        $trunk.StartCap = 'Round'; $trunk.EndCap = 'Round'
        $top = New-Object System.Drawing.PointF ($px + 26 * $k * $lean), (440 - 200 * $k)
        $g.DrawBezier($trunk, (New-Object System.Drawing.PointF $px, 452), (New-Object System.Drawing.PointF ($px - 4 * $lean), (440 - 90 * $k)), (New-Object System.Drawing.PointF ($px + 8 * $k * $lean), (440 - 160 * $k)), $top)
        foreach ($a in @(-175, -150, -122, -95, -62, -32, -6)) {
            $rad = $a * [math]::PI / 180
            $dx = [math]::Cos($rad); $up = -[math]::Sin($rad)   # 0..1 : palme plus ou moins dressee
            $len = (58 + 10 * $up) * $k
            $end = New-Object System.Drawing.PointF ($top.X + $dx * $len), ($top.Y + (26 - 30 * $up) * $k)
            $c1 = New-Object System.Drawing.PointF ($top.X + $dx * $len * 0.35), ($top.Y - (16 + 22 * $up) * $k)
            $c2 = New-Object System.Drawing.PointF ($top.X + $dx * $len * 0.75), ($top.Y - (8 + 18 * $up) * $k)
            $leaf = New-Object System.Drawing.Drawing2D.GraphicsPath
            $leaf.AddBezier($top, $c1, $c2, $end)
            $leaf.AddBezier($end, (New-Object System.Drawing.PointF $c2.X, ($c2.Y + 9 * $k)), (New-Object System.Drawing.PointF $c1.X, ($c1.Y + 7 * $k)), $top)
            $g.FillPath($ink, $leaf)
        }
        $g.FillEllipse($ink, $top.X - 5 * $k, $top.Y - 4 * $k, 10 * $k, 9 * $k)
    }
    if ($dark) { $g.FillRectangle((New-Object System.Drawing.SolidBrush (C 165 8 14 12)), 20, 60, 960, 540) }   # nuit
}

function SpacedText($gr, [string]$t, $font, $brush, [float]$cx, [float]$y, [float]$gap) {
    $sf = [System.Drawing.StringFormat]::GenericTypographic
    $ws = @(); $tot = 0
    foreach ($ch in $t.ToCharArray()) { $w = $gr.MeasureString([string]$ch, $font, 1000, $sf).Width; if ($ch -eq ' ') { $w = $font.Size * 0.35 }; $ws += $w; $tot += $w + $gap }
    $x = $cx - ($tot - $gap) / 2
    $i = 0
    foreach ($ch in $t.ToCharArray()) { $gr.DrawString([string]$ch, $font, $brush, $x, $y, $sf); $x += $ws[$i] + $gap; $i++ }
}

foreach ($dark in $false, $true) {
    $top = if ($dark) { @(20, 30, 24) } else { @(250, 240, 214) }
    $bot = if ($dark) { @(40, 30, 22) } else { @(246, 196, 140) }
    $bmp = New-Object System.Drawing.Bitmap ($W * $S), ($H * $S), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.ScaleTransform($S, $S)
    $g.SmoothingMode = 'AntiAlias'; $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)

    # Ombre douce
    for ($i = 18; $i -ge 1; $i--) {
        $r = New-Object System.Drawing.RectangleF ($card.X - $i), ($card.Y - $i + 8), ($card.Width + 2 * $i), ($card.Height + 2 * $i)
        $a = [int](9 * (1 - $i / 19.0) + 1)
        $g.FillPath((New-Object System.Drawing.SolidBrush (C $a 20 16 10)), (RoundPath $r (26 + $i)))
    }

    $cardPath = RoundPath $card 26
    $g.SetClip($cardPath)
    DrawCity $g $dark
    # A gauche, sous le panneau : degrade sable (haut) / peche (bas) qui se fond dans le paysage vers le milieu.
    for ($x = 20; $x -lt 580; $x += 2) {
        $k = if ($x -lt 400) { 1.0 } else { 1.0 - ($x - 400) / 180.0 }
        $k = $k * $k * (3 - 2 * $k)
        $a = [int](255 * $k)
        $vg = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 0, 59), (New-Object System.Drawing.PointF 0, 601), (C $a $top[0] $top[1] $top[2]), (C $a $bot[0] $bot[1] $bot[2])
        $g.FillRectangle($vg, $x, 60, 2, 540)
        $vg.Dispose()
    }

    # Accroche a droite, facon carte postale
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $ink = New-Object System.Drawing.SolidBrush $(if ($dark) { C 235 236 242 228 } else { C 235 255 250 236 })
    $f1 = New-Object System.Drawing.Font 'Segoe UI Light', 30, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
    $f2 = New-Object System.Drawing.Font 'Segoe UI', 13, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
    SpacedText $g 'GREETINGS FROM' $f1 $ink 715 182 9
    SpacedText $g 'SAN ANDREAS' $f1 $ink 715 222 13
    $lb = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 575, 0), (New-Object System.Drawing.PointF 855, 0), (C 255 36 158 74), (C 255 240 168 36)
    $g.FillRectangle($lb, 575, 270, 280, 2)
    SpacedText $g ('THE STORY IN CO-OP  ' + [char]0xB7 + '  PRE-ALPHA') $f2 (New-Object System.Drawing.SolidBrush $(if ($dark) { C 215 200 214 190 } else { C 225 255 244 220 })) 715 284 3.2

    # Panneau depoli : le fond sous le panneau, reduit puis agrandi (flou), voile clair ou sombre.
    $panelPath = RoundPath $panel 18
    $small = New-Object System.Drawing.Bitmap 45, 60
    $gs = [System.Drawing.Graphics]::FromImage($small)
    $gs.InterpolationMode = 'HighQualityBilinear'
    $gs.DrawImage($bmp, (New-Object System.Drawing.RectangleF 0, 0, 45, 60), (New-Object System.Drawing.RectangleF ($panel.X * $S), ($panel.Y * $S), ($panel.Width * $S), ($panel.Height * $S)), [System.Drawing.GraphicsUnit]::Pixel)
    $gs.Dispose()
    $g.SetClip($panelPath)
    $g.DrawImage($small, (New-Object System.Drawing.RectangleF ($panel.X - 6), ($panel.Y - 6), ($panel.Width + 12), ($panel.Height + 12)))
    $g.FillPath((New-Object System.Drawing.SolidBrush $(if ($dark) { C 190 18 26 20 } else { C 190 252 250 244 })), $panelPath)
    $g.ResetClip()
    $g.DrawPath((New-Object System.Drawing.Pen $(if ($dark) { C 80 120 220 140 } else { C 150 255 255 255 }), 1.5), $panelPath)
    $g.DrawPath((New-Object System.Drawing.Pen $(if ($dark) { C 90 120 220 140 } else { C 110 255 255 255 }), 1.5), $cardPath)

    # Logo : depasse du haut de la carte, halo doux derriere pour qu'il se lise sur le bureau.
    $logo = [System.Drawing.Image]::FromFile($logoFile)
    $src = AlphaBox $logo
    $dh = 170.0; $dw = $dh * $src.Width / $src.Height
    if ($dw -gt 220) { $dw = 220.0; $dh = $dw * $src.Height / $src.Width }
    $dst = New-Object System.Drawing.RectangleF (228 - $dw / 2), 8, $dw, $dh
    for ($i = 6; $i -ge 1; $i--) {
        $ia = New-Object System.Drawing.Imaging.ImageAttributes
        $cm = New-Object System.Drawing.Imaging.ColorMatrix
        $cm.Matrix00 = 0; $cm.Matrix11 = 0; $cm.Matrix22 = 0; $cm.Matrix33 = $(if ($dark) { 0.17 } else { 0.12 })
        $v = 0   # ombre noire : le logo de JD est blanc, il doit se lire aussi sur un bureau clair
        $cm.Matrix40 = $v; $cm.Matrix41 = $v; $cm.Matrix42 = $v
        $ia.SetColorMatrix($cm)
        foreach ($o in @(@(-$i, 0), @($i, 0), @(0, -$i), @(0, $i))) {
            $r = New-Object System.Drawing.Rectangle ([int]($dst.X + $o[0])), ([int]($dst.Y + $o[1])), ([int]$dst.Width), ([int]$dst.Height)
            $g.DrawImage($logo, $r, $src.X, $src.Y, $src.Width, $src.Height, [System.Drawing.GraphicsUnit]::Pixel, $ia)
        }
    }
    $g.DrawImage($logo, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)

    $g.Dispose()
    $out = Join-Path $ui $(if ($dark) { 'launcher-sombre.png' } else { 'launcher.png' })
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $logo.Dispose(); $bmp.Dispose()
    "Ecrit : $out"
}

# Icone du lanceur (launcher\sacoop.ico) : le logo sur une pastille verte, en PNG 256/48/32/16 dans un .ico.
$logo = [System.Drawing.Image]::FromFile($logoFile)
$box = AlphaBox $logo
$side = [math]::Max($box.Width, $box.Height) + 8
$cx = $box.X + $box.Width / 2; $cy = $box.Y + $box.Height / 2
$imgs = @()
foreach ($s in 256, 48, 32, 16) {
    $b = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $gg = [System.Drawing.Graphics]::FromImage($b)
    $gg.InterpolationMode = 'HighQualityBicubic'; $gg.SmoothingMode = 'AntiAlias'; $gg.PixelOffsetMode = 'HighQuality'
    $gg.Clear([System.Drawing.Color]::Transparent)
    $gb = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 0, 0), (New-Object System.Drawing.PointF 0, $s), (C 255 36 140 66), (C 255 20 70 36)
    $gg.FillEllipse($gb, 0, 0, $s - 1, $s - 1)
    $m = $s * 0.12
    $gg.DrawImage($logo, (New-Object System.Drawing.RectangleF $m, $m, ($s - 2 * $m), ($s - 2 * $m)), (New-Object System.Drawing.RectangleF ($cx - $side / 2), ($cy - $side / 2), $side, $side), [System.Drawing.GraphicsUnit]::Pixel)
    $gg.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $b.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $imgs += , @($s, $ms.ToArray())
    $b.Dispose()
}
$logo.Dispose()
$fs = [System.IO.File]::Create((Join-Path $PSScriptRoot 'sacoop.ico'))
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$imgs.Count)
$off = 6 + 16 * $imgs.Count
foreach ($i in $imgs) {
    $s = $i[0]; $len = $i[1].Length
    $w.Write([byte]($s % 256)); $w.Write([byte]($s % 256)); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$len); $w.Write([uint32]$off)
    $off += $len
}
foreach ($i in $imgs) { $w.Write($i[1]) }
$w.Close()
"Ecrit : sacoop.ico"

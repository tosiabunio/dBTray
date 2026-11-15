# Renders res\dBTray.ico: dBar's app icon (two rows of level segments on a dark plate) at the
# sizes Windows uses. Small sizes are drawn on whole pixels; large ones are scaled from the
# original 1024 px artwork.
#
#   powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1

param([string]$Out = (Join-Path $PSScriptRoot '..\res\dBTray.ico'))

Add-Type -AssemblyName System.Drawing

$green = [Drawing.Color]::FromArgb(0x34, 0xC7, 0x59)
$yellow = [Drawing.Color]::FromArgb(0xFF, 0xCC, 0x00)
$red = [Drawing.Color]::FromArgb(0xFF, 0x3B, 0x30)
$unlitAlpha = 77  # 0.3

# Whole-pixel geometry per size: segments, segment width, gap, bar height, gap between bars.
$snapped = @{
    16 = 5, 2, 1, 4, 2
    20 = 6, 2, 1, 5, 2
    24 = 6, 2, 1, 5, 2
    32 = 6, 3, 1, 7, 3
    40 = 6, 4, 1, 9, 3
    48 = 6, 4, 2, 11, 3
}

function New-RoundedRect([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $path
}

function Get-SegmentBrush([int]$index, [int]$count, [int]$row) {
    # The last segment is red, the one before it yellow. The top bar lights all the green ones,
    # the bottom bar one fewer.
    $color = if ($index -eq $count - 1) { $red } elseif ($index -eq $count - 2) { $yellow } else { $green }
    $lit = $index -lt ($count - 2 - $row)
    $alpha = if ($lit) { 255 } else { $unlitAlpha }
    New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb($alpha, $color))
}

function New-IconImage([int]$n) {
    $bitmap = New-Object Drawing.Bitmap $n, $n, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.PixelOffsetMode = 'Half'

    $plate = New-RoundedRect 0 0 $n $n ([Math]::Max(2, $n * 0.2))
    $top = [Drawing.Color]::FromArgb(0x2E, 0x31, 0x38)
    $bottom = [Drawing.Color]::FromArgb(0x16, 0x17, 0x1B)
    $fill = New-Object Drawing.Drawing2D.LinearGradientBrush (New-Object Drawing.PointF 0, 0), (New-Object Drawing.PointF 0, $n), $top, $bottom
    $g.FillPath($fill, $plate)

    if ($snapped.ContainsKey($n)) {
        $count, $w, $gap, $h, $rowGap = $snapped[$n]
        $x0 = [Math]::Floor(($n - ($count * $w + ($count - 1) * $gap)) / 2)
        $y0 = [Math]::Floor(($n - (2 * $h + $rowGap)) / 2)
        $g.SmoothingMode = 'None'
        foreach ($row in 0, 1) {
            for ($i = 0; $i -lt $count; $i++) {
                $g.FillRectangle((Get-SegmentBrush $i $count $row), $x0 + $i * ($w + $gap), $y0 + $row * ($h + $rowGap), $w, $h)
            }
        }
    } else {
        # dBar's artwork: an 824 px plate centred in 1024 px, 72 x 180 segments 108 px apart.
        $s = $n / 824
        foreach ($row in 0, 1) {
            for ($i = 0; $i -lt 6; $i++) {
                $segment = New-RoundedRect ((106 + 108 * $i) * $s) ((196 + 252 * $row) * $s) (72 * $s) (180 * $s) (16 * $s)
                $g.FillPath((Get-SegmentBrush $i 6 $row), $segment)
            }
        }
    }
    $g.Dispose()
    $bitmap
}

$images = foreach ($n in 16, 20, 24, 32, 40, 48, 64, 256) {
    $stream = New-Object IO.MemoryStream
    (New-IconImage $n).Save($stream, [Drawing.Imaging.ImageFormat]::Png)
    [pscustomobject]@{ Size = $n; Data = $stream.ToArray() }
}

# ICO container with PNG-compressed entries.
$file = [IO.File]::Create($Out)
$writer = New-Object IO.BinaryWriter $file
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $writer.Write([byte]($image.Size % 256))  # 0 means 256
    $writer.Write([byte]($image.Size % 256))
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$image.Data.Length)
    $writer.Write([uint32]$offset)
    $offset += $image.Data.Length
}
foreach ($image in $images) { $writer.Write($image.Data) }
$writer.Dispose()
Write-Host "Wrote $Out ($offset bytes)"

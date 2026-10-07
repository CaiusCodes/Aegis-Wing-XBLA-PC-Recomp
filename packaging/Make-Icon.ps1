# Draws packaging\aegis_wing.ico: a gold-rimmed roundel over the Europa
# nebula with the four player ships flying in formation (one ship at the
# smallest sizes, so it stays legible). Original artwork - no game assets.
# Every size is stored as PNG, which Windows Vista and later read natively.
[CmdletBinding()]
param([string]$OutputPath = (Join-Path $PSScriptRoot 'aegis_wing.ico'))

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Color([string]$Hex) { return [System.Drawing.ColorTranslator]::FromHtml($Hex) }

function Mix($A, $B, [double]$T) {
    $ca = if ($A -is [string]) { Color $A } else { $A }
    $cb = if ($B -is [string]) { Color $B } else { $B }
    return [System.Drawing.Color]::FromArgb(
        [int]($ca.R + ($cb.R - $ca.R) * $T), [int]($ca.G + ($cb.G - $ca.G) * $T), [int]($ca.B + ($cb.B - $ca.B) * $T))
}

# The ship silhouette used throughout Setup: an arrowhead pointing right.
function New-ShipPath([double]$Cx, [double]$Cy, [double]$Length) {
    $h = $Length * 0.55
    $points = [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF(($Cx - $Length / 2), $Cy)),
        (New-Object System.Drawing.PointF(($Cx + $Length * 0.18), ($Cy - $h / 2))),
        (New-Object System.Drawing.PointF(($Cx + $Length / 2), $Cy)),
        (New-Object System.Drawing.PointF(($Cx + $Length * 0.18), ($Cy + $h / 2))))
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddPolygon($points)
    return , $path
}

function Paint-Ship($g, [double]$Cx, [double]$Cy, [double]$Length, [string]$Hex) {
    $path = New-ShipPath $Cx $Cy $Length
    $rect = New-Object System.Drawing.RectangleF(($Cx - $Length / 2), ($Cy - $Length * 0.3), $Length, ($Length * 0.6))
    $fill = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, (Mix $Hex '#FFFFFF' 0.45), (Mix $Hex '#000000' 0.35), [single]90)
    $g.FillPath($fill, $path)
    $pen = New-Object System.Drawing.Pen((Mix $Hex '#000000' 0.65), [single][Math]::Max(0.6, $Length / 22))
    $g.DrawPath($pen, $path)
    # Cockpit glint.
    $glint = New-ShipPath ($Cx + $Length * 0.04) $Cy ($Length * 0.42)
    $shine = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(150, 255, 255, 255))
    $g.FillPath($shine, $glint)
    foreach ($o in @($shine, $glint, $pen, $fill, $path)) { $o.Dispose() }
}

function New-IconPng([int]$Size) {
    $bitmap = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $n = [double]$Size
    $rim = [Math]::Max(1.2, $n * 0.07)
    $outer = New-Object System.Drawing.RectangleF(([single]0.5), ([single]0.5), ([single]($n - 1)), ([single]($n - 1)))
    $gold = New-Object System.Drawing.Drawing2D.LinearGradientBrush($outer, (Color '#FFE9C0'), (Color '#8E4C1C'), [single]90)
    $g.FillEllipse($gold, $outer)

    # Nebula disc: warm glow up and to the right, black elsewhere.
    $inner = New-Object System.Drawing.RectangleF(([single]$rim), ([single]$rim), ([single]($n - 2 * $rim)), ([single]($n - 2 * $rim)))
    $disc = New-Object System.Drawing.Drawing2D.GraphicsPath
    $disc.AddEllipse($inner)
    $glow = New-Object System.Drawing.Drawing2D.PathGradientBrush($disc)
    $glow.CenterPoint = New-Object System.Drawing.PointF(([single]($n * 0.66)), ([single]($n * 0.36)))
    $glow.CenterColor = Color '#B9551D'
    $glow.SurroundColors = [System.Drawing.Color[]]@((Color '#070304'))
    $g.FillPath($glow, $disc)

    $g.SetClip($disc)
    if ($Size -ge 32) {
        # Diamond formation, as in Setup: red leads, blue and yellow on the
        # wings, green trailing, all heading right.
        $len = $n * 0.27
        Paint-Ship $g ($n * 0.40) ($n * 0.50) $len '#F2CF3A'
        Paint-Ship $g ($n * 0.55) ($n * 0.31) $len '#FF5A6E'
        Paint-Ship $g ($n * 0.55) ($n * 0.69) $len '#46C85A'
        Paint-Ship $g ($n * 0.70) ($n * 0.50) $len '#4E8DF5'
    }
    else {
        Paint-Ship $g ($n * 0.52) ($n * 0.50) ($n * 0.62) '#FF5A6E'
    }
    $g.ResetClip()

    foreach ($o in @($glow, $disc, $gold)) { $o.Dispose() }
    $g.Dispose()
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    return , $stream.ToArray()
}

$sizes = @(256, 128, 64, 48, 32, 24, 16)
$images = New-Object System.Collections.Generic.List[byte[]]
foreach ($size in $sizes) { $images.Add((New-IconPng $size)) }

$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($out)
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dim = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $writer.Write([byte]$dim)
    $writer.Write([byte]$dim)
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$i].Length)
    $writer.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write($image) }
$writer.Flush()
[System.IO.File]::WriteAllBytes($OutputPath, $out.ToArray())
Write-Host ('Icon written: {0} ({1:N0} bytes, {2} sizes)' -f $OutputPath, $out.Length, $sizes.Count)

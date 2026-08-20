#Requires -Version 5.1
<#
Regenerates assets/app.ico from assets/rune_1008.png.

The source artwork is 104x104 RGBA. The ICO carries 16/32/48/256 entries,
each stored PNG-compressed (Vista and later decode PNG icon entries), so
the file stays small and the alpha edge survives.

Run from anywhere: paths resolve from the repo root.
#>
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root 'assets\rune_1008.png'
$dst = Join-Path $root 'assets\app.ico'

$source = [System.Drawing.Image]::FromFile($src)
try {
    $sizes = @(16, 32, 48, 256)
    $pngs = New-Object System.Collections.ArrayList
    foreach ($size in $sizes) {
        $bmp = New-Object System.Drawing.Bitmap $size, $size
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.DrawImage($source, 0, 0, $size, $size)
        $g.Dispose()
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        [void]$pngs.Add($ms.ToArray())
        $ms.Dispose()
        $bmp.Dispose()
    }

    $out = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter $out
    $w.Write([UInt16]0)              # reserved
    $w.Write([UInt16]1)              # type: icon
    $w.Write([UInt16]$sizes.Count)   # entry count

    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $dim = $sizes[$i]
        $dimByte = [Byte]$(if ($dim -ge 256) { 0 } else { $dim })
        $w.Write($dimByte)
        $w.Write($dimByte)
        $w.Write([Byte]0)            # palette
        $w.Write([Byte]0)            # reserved
        $w.Write([UInt16]1)          # planes
        $w.Write([UInt16]32)         # bpp
        $w.Write([UInt32]$pngs[$i].Length)
        $w.Write([UInt32]$offset)
        $offset += $pngs[$i].Length
    }
    foreach ($png in $pngs) { $w.Write($png) }
    $w.Flush()
    [System.IO.File]::WriteAllBytes($dst, $out.ToArray())
    $w.Dispose()
    $out.Dispose()
} finally {
    $source.Dispose()
}
Write-Host "wrote $dst"

param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$OutDir = ''
)

Add-Type -AssemblyName System.Drawing

function Get-NearestPaletteIndex {
    param(
        [System.Drawing.Color[]]$Palette,
        [System.Drawing.Color]$Color,
        [hashtable]$Cache,
        [int]$SkipIndex = -1
    )

    $key = (($Color.R -shl 16) -bor ($Color.G -shl 8) -bor $Color.B).ToString() + ':' + $SkipIndex.ToString()
    if($Cache.ContainsKey($key)) {
        return [byte]$Cache[$key]
    }

    $bestIndex = 0
    $bestDistance = [int64]::MaxValue
    for($i = 0; $i -lt $Palette.Length; $i++) {
        if($i -eq $SkipIndex) {
            continue
        }
        $p = $Palette[$i]
        $dr = [int]$Color.R - [int]$p.R
        $dg = [int]$Color.G - [int]$p.G
        $db = [int]$Color.B - [int]$p.B
        $distance = $dr * $dr + $dg * $dg + $db * $db
        if($distance -lt $bestDistance) {
            $bestDistance = $distance
            $bestIndex = $i
        }
    }

    $Cache[$key] = [byte]$bestIndex
    return [byte]$bestIndex
}

function Test-PointInRects {
    param(
        [int]$X,
        [int]$Y,
        [int[][]]$Rects
    )

    foreach($rect in $Rects) {
        if($X -ge $rect[0] -and $X -lt ($rect[0] + $rect[2]) -and
           $Y -ge $rect[1] -and $Y -lt ($rect[1] + $rect[3])) {
            return $true
        }
    }
    return $false
}

function Read-IndexedBytes {
    param([System.Drawing.Bitmap]$Bitmap)

    $rect = [System.Drawing.Rectangle]::new(0, 0, $Bitmap.Width, $Bitmap.Height)
    $data = $Bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format8bppIndexed)
    try {
        $bytes = New-Object byte[] ($data.Stride * $Bitmap.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        return @{ Bytes = $bytes; Stride = $data.Stride }
    } finally {
        $Bitmap.UnlockBits($data)
    }
}

function Write-IndexedPng {
    param(
        [string]$Path,
        [int]$Width,
        [int]$Height,
        [System.Drawing.Color[]]$Palette,
        [byte[]]$Pixels
    )

    $bitmap = [System.Drawing.Bitmap]::new($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format8bppIndexed)
    try {
        $newPalette = $bitmap.Palette
        for($i = 0; $i -lt [Math]::Min($newPalette.Entries.Length, $Palette.Length); $i++) {
            $newPalette.Entries[$i] = $Palette[$i]
        }
        $bitmap.Palette = $newPalette

        $rect = [System.Drawing.Rectangle]::new(0, 0, $Width, $Height)
        $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format8bppIndexed)
        try {
            $buffer = New-Object byte[] ($data.Stride * $Height)
            for($y = 0; $y -lt $Height; $y++) {
                [Array]::Copy($Pixels, $y * $Width, $buffer, $y * $data.Stride, $Width)
            }
            [System.Runtime.InteropServices.Marshal]::Copy($buffer, 0, $data.Scan0, $buffer.Length)
        } finally {
            $bitmap.UnlockBits($data)
        }

        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bitmap.Dispose()
    }
}

function Repair-Strip {
    param(
        [string]$SourceName,
        [int]$FrameCount,
        [int]$BaseX,
        [int]$BaseY,
        [int[][]]$Matches,
        [int]$TransparentIndex,
        [int]$Threshold,
        [int[][][]]$FrameMasks,
        [string]$OutputPath
    )

    $sourcePath = Join-Path $Root "data\$SourceName"
    $backgroundPath = Join-Path $Root 'data\PaulAtreidesMentat.png'
    $source = [System.Drawing.Bitmap]::new($sourcePath)
    $background = [System.Drawing.Bitmap]::new($backgroundPath)
    try {
        if($source.PixelFormat -ne [System.Drawing.Imaging.PixelFormat]::Format8bppIndexed) {
            throw "$SourceName must be indexed 8bpp before repair."
        }

        $palette = $source.Palette.Entries
        $frameWidth = [Math]::Floor($source.Width / $FrameCount)
        $frameHeight = $source.Height
        $sourceData = Read-IndexedBytes $source
        $pixels = New-Object byte[] ($frameWidth * $FrameCount * $frameHeight)
        $cache = @{}
        $thresholdSquared = $Threshold * $Threshold
        for($frame = 0; $frame -lt $FrameCount; $frame++) {
            $matchX = $Matches[$frame][0]
            $matchY = $Matches[$frame][1]
            $sourceOffsetX = $BaseX - $matchX
            $sourceOffsetY = $BaseY - $matchY
            $masks = $FrameMasks[$frame]

            for($y = 0; $y -lt $frameHeight; $y++) {
                for($x = 0; $x -lt $frameWidth; $x++) {
                    $outIndex = [byte]$TransparentIndex
                    if(-not (Test-PointInRects $x $y $masks)) {
                        $pixels[$y * ($frameWidth * $FrameCount) + $frame * $frameWidth + $x] = $outIndex
                        continue
                    }

                    $sx = $x + $sourceOffsetX
                    $sy = $y + $sourceOffsetY
                    if($sx -ge 0 -and $sx -lt $frameWidth -and $sy -ge 0 -and $sy -lt $frameHeight) {
                        $srcIndex = $sourceData.Bytes[$sy * $sourceData.Stride + $frame * $frameWidth + $sx]
                        $srcColor = $palette[$srcIndex]
                        $targetColor = $background.GetPixel($BaseX + $x, $BaseY + $y)
                        $dr = [int]$srcColor.R - [int]$targetColor.R
                        $dg = [int]$srcColor.G - [int]$targetColor.G
                        $db = [int]$srcColor.B - [int]$targetColor.B
                        if(($dr * $dr + $dg * $dg + $db * $db) -ge $thresholdSquared) {
                            if($srcIndex -eq $TransparentIndex) {
                                $outIndex = Get-NearestPaletteIndex $palette $srcColor $cache $TransparentIndex
                            } else {
                                $outIndex = $srcIndex
                            }
                        }
                    }

                    $pixels[$y * ($frameWidth * $FrameCount) + $frame * $frameWidth + $x] = $outIndex
                }
            }
        }

        Write-IndexedPng $OutputPath ($frameWidth * $FrameCount) $frameHeight $palette $pixels
    } finally {
        $source.Dispose()
        $background.Dispose()
    }
}

if([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $Root 'build-msys\tmp\paul-mentat-fix'
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$eyeMasks = @(
    ,@(@(6, 0, 88, 31)),
    ,@(@(6, 0, 88, 31)),
    ,@(@(6, 0, 88, 31)),
    ,@(@(6, 0, 88, 31)),
    ,@(@(6, 0, 88, 31))
)

Repair-Strip 'PaulAtreidesEyes.png' 5 120 116 @(
    @(120,116),
    @(120,118),
    @(118,114),
    @(118,114),
    @(117,113)
) 196 32 $eyeMasks (Join-Path $OutDir 'PaulAtreidesEyes.png')

$mouthMasks = @(
    ,@(@(30, 7, 58, 18)),
    ,@(@(30, 7, 58, 18)),
    ,@(@(28, 5, 62, 22)),
    ,@(@(25, 1, 67, 28)),
    ,@(@(25, 4, 67, 23))
)

Repair-Strip 'PaulAtreidesMouth.png' 5 119 171 @(
    @(119,171),
    @(118,170),
    @(115,171),
    @(117,177),
    @(117,171)
) 18 30 $mouthMasks (Join-Path $OutDir 'PaulAtreidesMouth.png')

Get-ChildItem $OutDir -Filter 'PaulAtreides*.png' | Select-Object FullName, Length

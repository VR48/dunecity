param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$dataDir = Join-Path $RepoRoot "data"
$modDataDir = Join-Path $RepoRoot "mods\Tornie\data"
$modDir = Split-Path -Parent $modDataDir
$campaignDir = Join-Path $modDir "campaign"
$sourcePak = Join-Path $modDataDir "Tornie.PAK"

function Read-Pak {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $offset = 0
    $entries = New-Object System.Collections.Generic.List[object]

    while($true) {
        $start = [System.BitConverter]::ToUInt32($bytes, $offset)
        $offset += 4
        if($start -eq 0) {
            break
        }

        $nameStart = $offset
        while($bytes[$offset] -ne 0) {
            $offset++
        }

        $name = [System.Text.Encoding]::ASCII.GetString($bytes, $nameStart, $offset - $nameStart)
        $offset++
        $entries.Add([pscustomobject]@{ Name = $name; Start = [int]$start; End = 0 })
    }

    $data = @{}
    $order = New-Object System.Collections.Generic.List[string]

    for($i = 0; $i -lt $entries.Count; $i++) {
        $end = if($i + 1 -lt $entries.Count) { [int]$entries[$i + 1].Start } else { $bytes.Length }
        $entries[$i].End = $end
        $length = $end - [int]$entries[$i].Start
        $chunk = New-Object byte[] $length
        [System.Array]::Copy($bytes, [int]$entries[$i].Start, $chunk, 0, $length)
        $data[$entries[$i].Name] = $chunk
        $order.Add($entries[$i].Name)
    }

    return [pscustomobject]@{ Data = $data; Order = $order }
}

function Write-Pak {
    param(
        [string]$Path,
        [System.Collections.Generic.List[string]]$Order,
        [hashtable]$Data
    )

    $headerSize = 4
    foreach($name in $Order) {
        $headerSize += 4 + [System.Text.Encoding]::ASCII.GetByteCount($name) + 1
    }

    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    try {
        $dataOffset = 0
        foreach($name in $Order) {
            $entryOffset = [System.BitConverter]::GetBytes([uint32]($headerSize + $dataOffset))
            $stream.Write($entryOffset, 0, $entryOffset.Length)

            $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($name)
            $stream.Write($nameBytes, 0, $nameBytes.Length)
            $stream.WriteByte(0)

            $dataOffset += $Data[$name].Length
        }

        $zero = [System.BitConverter]::GetBytes([uint32]0)
        $stream.Write($zero, 0, $zero.Length)

        foreach($name in $Order) {
            $chunk = $Data[$name]
            $stream.Write($chunk, 0, $chunk.Length)
        }
    } finally {
        $stream.Dispose()
    }
}

function Test-CampaignFileName {
    param([string]$Name)

    $upper = $Name.ToUpperInvariant()
    return $upper.EndsWith(".INI") -and ($upper.StartsWith("REGION") -or $upper.StartsWith("SCEN"))
}

function Add-PakEntry {
    param(
        [System.Collections.Generic.List[string]]$Order,
        [hashtable]$Data,
        [System.IO.FileInfo]$File
    )

    if(-not $Data.ContainsKey($File.Name)) {
        $Order.Add($File.Name)
    }

    $Data[$File.Name] = [System.IO.File]::ReadAllBytes($File.FullName)
}

function Convert-PalEntry {
    param([byte]$Value)
    return [byte][Math]::Round(([double]$Value) * 255.0 / 63.0)
}

function Get-IbmPalette {
    param([hashtable]$PakData)

    if(-not $PakData.ContainsKey("IBM.PAL")) {
        throw "IBM.PAL is missing from Tornie.PAK"
    }

    $palBytes = $PakData["IBM.PAL"]
    if($palBytes.Length -lt 768) {
        throw "IBM.PAL is too small"
    }

    $colors = New-Object System.Collections.Generic.List[System.Drawing.Color]
    for($i = 0; $i -lt 256; $i++) {
        $r = Convert-PalEntry $palBytes[$i * 3]
        $g = Convert-PalEntry $palBytes[$i * 3 + 1]
        $b = Convert-PalEntry $palBytes[$i * 3 + 2]
        $colors.Add([System.Drawing.Color]::FromArgb(255, $r, $g, $b))
    }

    return $colors
}

function Find-NearestPaletteIndex {
    param(
        [System.Drawing.Color]$Color,
        [System.Collections.Generic.List[System.Drawing.Color]]$Palette
    )

    $bestIndex = 1
    $bestDistance = [int64]::MaxValue
    for($i = 1; $i -lt $Palette.Count; $i++) {
        $p = $Palette[$i]
        $dr = [int]$Color.R - [int]$p.R
        $dg = [int]$Color.G - [int]$p.G
        $db = [int]$Color.B - [int]$p.B
        $distance = [int64]($dr * $dr + $dg * $dg + $db * $db)
        if($distance -lt $bestDistance) {
            $bestDistance = $distance
            $bestIndex = $i
            if($distance -eq 0) {
                break
            }
        }
    }

    return [byte]$bestIndex
}

function Save-IndexedPng {
    param(
        [string]$Source,
        [string]$Destination,
        [System.Collections.Generic.List[System.Drawing.Color]]$Palette
    )

    $sourceBitmap = [System.Drawing.Bitmap]::FromFile($Source)
    try {
        $indexed = New-Object System.Drawing.Bitmap $sourceBitmap.Width, $sourceBitmap.Height, ([System.Drawing.Imaging.PixelFormat]::Format8bppIndexed)
        $pal = $indexed.Palette
        for($i = 0; $i -lt 256; $i++) {
            $pal.Entries[$i] = $Palette[$i]
        }
        $indexed.Palette = $pal

        $rect = New-Object System.Drawing.Rectangle 0, 0, $indexed.Width, $indexed.Height
        $bits = $indexed.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format8bppIndexed)
        try {
            $buffer = New-Object byte[] ($bits.Stride * $indexed.Height)
            for($y = 0; $y -lt $indexed.Height; $y++) {
                for($x = 0; $x -lt $indexed.Width; $x++) {
                    $c = $sourceBitmap.GetPixel($x, $y)
                    $buffer[$y * $bits.Stride + $x] = if($c.A -lt 128) { [byte]0 } else { Find-NearestPaletteIndex $c $Palette }
                }
            }
            [System.Runtime.InteropServices.Marshal]::Copy($buffer, 0, $bits.Scan0, $buffer.Length)
        } finally {
            $indexed.UnlockBits($bits)
        }

        $indexed.Save($Destination, [System.Drawing.Imaging.ImageFormat]::Png)
        $indexed.Dispose()
    } finally {
        $sourceBitmap.Dispose()
    }
}

$pak = Read-Pak $sourcePak
$palettePak = $pak
if(-not $palettePak.Data.ContainsKey("IBM.PAL")) {
    $palettePak = Read-Pak (Join-Path $dataDir "DUNE.PAK")
}
$palette = Get-IbmPalette $palettePak.Data

# The engine expects a final eight-direction indexed atlas for ground units.
# Keep the approved Sonic Trike reference as the runtime mask so it follows
# the same proven loading path as RocketTrikeMask.png.
$sonicTrikeReference = Join-Path $RepoRoot "mods\Tornie\source-sprites\vehicles\SonicTrike_reference.png"
$sonicTrikeMask = Join-Path $modDataDir "SonicTrikeMask.png"
if(Test-Path -LiteralPath $sonicTrikeReference) {
    Copy-Item -LiteralPath $sonicTrikeReference -Destination $sonicTrikeMask -Force
    Write-Host "Refreshed SonicTrikeMask.png"
}

$spritesToIndex = @(
    "SonicTrikeMask.png",
    "Worfinery.png",
    "TechCenter.png",
    "Scoutpost.png",
    "Advanced_Power_Plant.png",
    "advanced_power_2x3.png"
)

foreach($name in $spritesToIndex) {
    $source = Join-Path $modDataDir $name
    if(Test-Path -LiteralPath $source) {
        $temp = Join-Path $modDataDir ("." + $name + ".tmp.png")
        Save-IndexedPng $source $temp $palette
        Move-Item -LiteralPath $temp -Destination $source -Force
        Copy-Item -LiteralPath $source -Destination (Join-Path $dataDir $name) -Force
        Write-Host "Indexed $name"
    }
}

# Rebuild from the current Tornie loose files instead of carrying entries from
# the old PAK forward.  This prevents stale vanilla/fallback assets and duplicate
# scenario entries from shadowing corrected Tornie assets.
$fileData = @{}
$fileOrder = New-Object System.Collections.Generic.List[string]

Get-ChildItem -LiteralPath $modDataDir -File | Sort-Object Name | ForEach-Object {
    if($_.Name.ToUpperInvariant() -eq "TORNIE.PAK") {
        return
    }

    if(Test-CampaignFileName $_.Name) {
        return
    }

    Add-PakEntry $fileOrder $fileData $_
}

if(Test-Path -LiteralPath $campaignDir) {
    Get-ChildItem -LiteralPath $campaignDir -File | Sort-Object Name | ForEach-Object {
        Add-PakEntry $fileOrder $fileData $_
    }
}

$tmpPak = Join-Path $modDataDir ".Tornie.PAK.tmp"
Write-Pak $tmpPak $fileOrder $fileData
Move-Item -LiteralPath $tmpPak -Destination $sourcePak -Force
Copy-Item -LiteralPath $sourcePak -Destination (Join-Path $dataDir "Tornie.PAK") -Force

Write-Host "Rebuilt Tornie.PAK with $($fileOrder.Count) entries"

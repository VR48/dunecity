param(
    [string]$EmsdkRoot = 'D:\BotServer\Tools\emsdk',
    [string]$BuildRoot = 'D:\BotServer\Builds\Dune2R-web',
    [string]$WebsiteRoot = 'D:\BotServer\GitHub\dunelegacy.com\website'
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Emcmake = Join-Path $EmsdkRoot 'upstream\emscripten\emcmake.exe'
$PlayRoot = Join-Path $WebsiteRoot 'play'

if (-not (Test-Path -LiteralPath $Emcmake)) {
    throw "Emscripten is not installed at $EmsdkRoot. Run scripts/install-emsdk.ps1 first."
}

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
& $Emcmake cmake -S $RepoRoot -B $BuildRoot -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DDUNECITY_BUILD_TESTS=OFF `
    -DDUNECITY_ENABLE_PCH=OFF
if ($LASTEXITCODE -ne 0) { throw "Emscripten CMake configure failed with exit code $LASTEXITCODE" }

cmake --build $BuildRoot --target dunecity --parallel 8
if ($LASTEXITCODE -ne 0) { throw "Emscripten build failed with exit code $LASTEXITCODE" }

New-Item -ItemType Directory -Force -Path $PlayRoot | Out-Null
$ProjectVersion = Select-String -LiteralPath (Join-Path $RepoRoot 'CMakeLists.txt') `
    -Pattern '^project\(DuneCity VERSION ([0-9.]+)' | ForEach-Object { $_.Matches[0].Groups[1].Value }
if (-not $ProjectVersion) { throw 'Could not read the DuneCity project version from CMakeLists.txt.' }

$PublishedFiles = @(
    '.htaccess',
    'index.html',
    'shell.css',
    'shell.js',
    'dunecity.js',
    'dunecity.wasm',
    'dunecity.data',
    'build.json'
)
Get-ChildItem -LiteralPath $PlayRoot -File | Where-Object { $_.Name -notin $PublishedFiles } | Remove-Item -Force

$OutputRoot = Join-Path $BuildRoot 'bin'
$Files = @('dunecity.html', 'dunecity.js', 'dunecity.wasm', 'dunecity.data')
foreach ($File in $Files) {
    $Source = Join-Path $OutputRoot $File
    if (-not (Test-Path -LiteralPath $Source)) { throw "Missing browser artifact: $Source" }
    $DestinationName = if ($File -eq 'dunecity.html') { 'index.html' } else { $File }
    Copy-Item -LiteralPath $Source -Destination (Join-Path $PlayRoot $DestinationName) -Force
}

$ShellFiles = @('shell.css', 'shell.js')
foreach ($File in $ShellFiles) {
    $Source = Join-Path (Join-Path $RepoRoot 'web') $File
    if (-not (Test-Path -LiteralPath $Source)) { throw "Missing browser shell asset: $Source" }
    Copy-Item -LiteralPath $Source -Destination (Join-Path $PlayRoot $File) -Force
}

$ArtifactNames = @('index.html', 'shell.css', 'shell.js', 'dunecity.js', 'dunecity.wasm', 'dunecity.data')
$ArtifactHashes = [ordered]@{}
foreach ($File in $ArtifactNames) {
    $ArtifactHashes[$File] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PlayRoot $File)).Hash.ToLowerInvariant()
}

$BuildInfo = @{
    version = $ProjectVersion
    builtAtUtc = [DateTime]::UtcNow.ToString('o')
    artifacts = $ArtifactNames
    sha256 = $ArtifactHashes
} | ConvertTo-Json -Depth 4
$BuildInfoPath = Join-Path $PlayRoot 'build.json'
[IO.File]::WriteAllText(
    $BuildInfoPath,
    $BuildInfo + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))

Write-Host "Browser build packaged at $PlayRoot"

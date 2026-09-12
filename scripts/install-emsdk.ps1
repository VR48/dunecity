param(
    [string]$EmsdkRoot = 'D:\BotServer\Tools\emsdk',
    [string]$Version = 'latest'
)

$ErrorActionPreference = 'Stop'
$Parent = Split-Path -Parent $EmsdkRoot
New-Item -ItemType Directory -Force -Path $Parent | Out-Null

if (-not (Test-Path -LiteralPath (Join-Path $EmsdkRoot '.git'))) {
    git clone https://github.com/emscripten-core/emsdk.git $EmsdkRoot
    if ($LASTEXITCODE -ne 0) { throw "Could not clone emsdk" }
}

Push-Location $EmsdkRoot
try {
    & .\emsdk.bat install $Version
    if ($LASTEXITCODE -ne 0) { throw "Could not install Emscripten $Version" }
    & .\emsdk.bat activate $Version
    if ($LASTEXITCODE -ne 0) { throw "Could not activate Emscripten $Version" }
} finally {
    Pop-Location
}

Write-Host "Emscripten $Version is ready at $EmsdkRoot"

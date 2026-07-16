if ([string]::IsNullOrWhiteSpace($env:JAVA_HOME)) {
    throw "Set JAVA_HOME to your JDK before running this script."
}

if ([string]::IsNullOrWhiteSpace($env:ANDROID_HOME)) {
    $env:ANDROID_HOME = $env:ANDROID_SDK_ROOT
}
if ([string]::IsNullOrWhiteSpace($env:ANDROID_HOME)) {
    throw "Set ANDROID_HOME or ANDROID_SDK_ROOT to your Android SDK before running this script."
}
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME

if ([string]::IsNullOrWhiteSpace($env:ANDROID_NDK_HOME)) {
    $ndkRoot = Join-Path $env:ANDROID_HOME "ndk"
    if (Test-Path -LiteralPath $ndkRoot) {
        $latestNdk = Get-ChildItem -LiteralPath $ndkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if ($null -ne $latestNdk) {
            $env:ANDROID_NDK_HOME = $latestNdk.FullName
        }
    }
}
if ([string]::IsNullOrWhiteSpace($env:ANDROID_NDK_HOME)) {
    throw "Set ANDROID_NDK_HOME or install an NDK under `$ANDROID_HOME/ndk."
}

$env:Path = "$env:JAVA_HOME\bin;$env:ANDROID_HOME\platform-tools;$env:ANDROID_HOME\cmdline-tools\latest\bin;$env:Path"

Write-Host "JAVA_HOME=$env:JAVA_HOME"
Write-Host "ANDROID_HOME=$env:ANDROID_HOME"
Write-Host "ANDROID_NDK_HOME=$env:ANDROID_NDK_HOME"

@echo off
if "%JAVA_HOME%"=="" (
    echo Set JAVA_HOME to your JDK before running this script.
    exit /b 1
)
if "%ANDROID_HOME%"=="" (
    if not "%ANDROID_SDK_ROOT%"=="" set "ANDROID_HOME=%ANDROID_SDK_ROOT%"
)
if "%ANDROID_HOME%"=="" (
    echo Set ANDROID_HOME or ANDROID_SDK_ROOT to your Android SDK before running this script.
    exit /b 1
)
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
if "%ANDROID_NDK_HOME%"=="" (
    for /f "delims=" %%D in ('dir /b /ad /o-n "%ANDROID_HOME%\ndk" 2^>nul') do (
        set "ANDROID_NDK_HOME=%ANDROID_HOME%\ndk\%%D"
        goto :found_ndk
    )
)
:found_ndk
if "%ANDROID_NDK_HOME%"=="" (
    echo Set ANDROID_NDK_HOME or install an NDK under %%ANDROID_HOME%%\ndk.
    exit /b 1
)
set "PATH=%JAVA_HOME%\bin;%ANDROID_HOME%\platform-tools;%ANDROID_HOME%\cmdline-tools\latest\bin;%PATH%"

echo JAVA_HOME=%JAVA_HOME%
echo ANDROID_HOME=%ANDROID_HOME%
echo ANDROID_NDK_HOME=%ANDROID_NDK_HOME%

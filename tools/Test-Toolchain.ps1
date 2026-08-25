#Requires -Version 7.0
<#
.SYNOPSIS
    Verifies the pinned toolchain before a build.

.DESCRIPTION
    An unpinned toolchain means a CI runner image update silently changes the
    binary, and then a performance regression cannot be attributed to a code
    change. This script fails early with a message naming the missing
    component, rather than letting the build produce a subtly different binary.

    Every failure prints the actual value next to the expected one. A check
    that only says "mismatch" costs a round trip to diagnose.
#>
[CmdletBinding()]
param(
    # Skip the .NET check. The C++ build does not need .NET at all; it is only
    # required for the Velopack `vpk` packaging tool (Phase 6).
    [switch] $SkipDotnet
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$expected = [ordered]@{
    visualStudioPrefix = '18.9.'
    vcToolsVersion     = '14.51.36231'
    compilerPrefix     = '19.51.362'
    windowsSdkVersion  = '10.0.26100.0'
    dotnetSdkPrefix    = '9.0.3'
}

$problems = [System.Collections.Generic.List[string]]::new()

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "vswhere.exe not found at '$vswhere'. Install Visual Studio 2026 with the Desktop development with C++ workload."
}

$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -format json | ConvertFrom-Json | Select-Object -First 1

if (-not $installation) {
    throw 'No Visual Studio 2026 installation with the C++ x64 toolset was found. Install the "Desktop development with C++" workload, or the VC.Tools.x86.x64 component in Build Tools.'
}

$msbuildPath  = Join-Path $installation.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
$compilerPath = Join-Path $installation.installationPath "VC\Tools\MSVC\$($expected.vcToolsVersion)\bin\Hostx64\x64\cl.exe"
$sdkHeader    = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include\$($expected.windowsSdkVersion)\um\Windows.h"

$actual = [ordered]@{
    visualStudio = $installation.installationVersion
    installPath  = $installation.installationPath
    msbuild      = if (Test-Path -LiteralPath $msbuildPath) { (Get-Item -LiteralPath $msbuildPath).VersionInfo.FileVersion } else { '(not found)' }
    vcTools      = if (Test-Path -LiteralPath $compilerPath) { $expected.vcToolsVersion } else { '(not found)' }
    compiler     = if (Test-Path -LiteralPath $compilerPath) { (Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion } else { '(not found)' }
    windowsSdk   = if (Test-Path -LiteralPath $sdkHeader) { $expected.windowsSdkVersion } else { '(not found)' }
    dotnetSdk    = '(skipped)'
}

if (-not $actual.visualStudio.StartsWith($expected.visualStudioPrefix, [StringComparison]::Ordinal)) {
    $problems.Add("Visual Studio $($expected.visualStudioPrefix)x expected, found $($actual.visualStudio). Update Visual Studio 2026, or adjust the pin in this script and re-record the performance baseline (issue #13).")
}

if ($actual.msbuild -eq '(not found)') {
    $problems.Add("MSBuild.exe not found at '$msbuildPath'. The Microsoft.Component.MSBuild component is missing from this installation.")
} elseif (-not $actual.msbuild.StartsWith($expected.visualStudioPrefix, [StringComparison]::Ordinal)) {
    $problems.Add("MSBuild $($expected.visualStudioPrefix)x expected, found $($actual.msbuild).")
}

if ($actual.vcTools -eq '(not found)') {
    $installed = Get-ChildItem -Path (Join-Path $installation.installationPath 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Name
    $problems.Add("VC tools $($expected.vcToolsVersion) not installed. Present: $(if ($installed) { $installed -join ', ' } else { 'none' }). Install that exact version via the Visual Studio Installer's individual components, or change VCToolsVersion in src\dhepz.vcxproj and re-record the baseline.")
} elseif (-not $actual.compiler.StartsWith($expected.compilerPrefix, [StringComparison]::Ordinal)) {
    $problems.Add("cl.exe $($expected.compilerPrefix)x expected, found $($actual.compiler). The VC tools directory name matches the pin but the compiler inside it does not.")
}

if ($actual.windowsSdk -eq '(not found)') {
    $problems.Add("Windows SDK $($expected.windowsSdkVersion) not found (looked for '$sdkHeader'). Install it via the Visual Studio Installer or the standalone Windows SDK installer.")
}

if (-not $SkipDotnet) {
    # `dotnet --version` resolves through global.json, so this validates the
    # pin as the build will actually experience it, not just what is installed.
    $dotnetVersion = $null
    try {
        $dotnetVersion = (& dotnet --version 2>&1 | Out-String).Trim()
    } catch {
        $dotnetVersion = $null
    }

    if ($LASTEXITCODE -ne 0 -or -not $dotnetVersion -or $dotnetVersion -notmatch '^\d+\.\d+\.\d+') {
        $problems.Add("The .NET SDK pinned in global.json could not be resolved. Install .NET SDK $($expected.dotnetSdkPrefix)xx, or pass -SkipDotnet: .NET is only needed for Velopack packaging (Phase 6), not for the C++ build. Output was: $dotnetVersion")
        $actual.dotnetSdk = '(unresolved)'
    } else {
        $actual.dotnetSdk = $dotnetVersion
        if (-not $dotnetVersion.StartsWith($expected.dotnetSdkPrefix, [StringComparison]::Ordinal)) {
            $problems.Add(".NET SDK $($expected.dotnetSdkPrefix)xx expected, found $dotnetVersion. Adjust global.json.")
        }
    }
}

[pscustomobject]$actual | Format-List

if ($problems.Count -gt 0) {
    Write-Host ''
    Write-Host "Toolchain does not match the pin ($($problems.Count) problem(s)):" -ForegroundColor Red
    foreach ($problem in $problems) {
        Write-Host "  - $problem" -ForegroundColor Red
    }
    exit 1
}

Write-Host 'Toolchain matches the pin.' -ForegroundColor Green
exit 0

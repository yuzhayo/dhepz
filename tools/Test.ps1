#Requires -Version 7.0
<#
.SYNOPSIS
    Builds and runs the unit tests.

.DESCRIPTION
    Builds each requested configuration and runs the test runner against it.

    If the runner is missing the script exits 2 rather than 0: a green result
    from a test suite that did not run is worse than a red one.

.EXAMPLE
    ./tools/Test.ps1
    Builds and tests Debug and Release.

.EXAMPLE
    ./tools/Test.ps1 -Configuration Debug -Filter json
    Debug only, tests whose name contains "json".
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string] $Configuration = 'All',

    [string] $Filter,

    # Emit JUnit XML for CI to consume.
    [string] $JUnitPath,

    # Test an already-built tree.
    [switch] $NoBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
# The @() is load-bearing: PowerShell unwraps a single-element array returned
# from an if-expression into a scalar, and Set-StrictMode then makes .Count on
# that string a hard error.
$configurations = @(if ($Configuration -eq 'All') { 'Debug', 'Release' } else { $Configuration })

foreach ($current in $configurations) {
    if (-not $NoBuild) {
        & (Join-Path $PSScriptRoot 'Build.ps1') -Configuration $current
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    $runner = Join-Path $repositoryRoot "build\x64\$current\dhepz_tests.exe"
    if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
        Write-Host "No test runner at '$runner'." -ForegroundColor Yellow
        Write-Host 'The build should have produced it. Reporting this rather than exiting 0, because a pass from a suite that did not run is worse than a failure.' -ForegroundColor Yellow
        exit 2
    }

    $arguments = @()
    if ($PSBoundParameters.ContainsKey('Filter')) {
        $arguments += @('--filter', $Filter)
    }
    if ($PSBoundParameters.ContainsKey('JUnitPath')) {
        $junit = if ($configurations.Count -gt 1) {
            # One file per configuration, or the second run overwrites the first
            # and CI reports half the results.
            [IO.Path]::Combine(
                [IO.Path]::GetDirectoryName($JUnitPath),
                "$([IO.Path]::GetFileNameWithoutExtension($JUnitPath)).$current$([IO.Path]::GetExtension($JUnitPath))")
        } else {
            $JUnitPath
        }
        $junitDirectory = Split-Path -Parent $junit
        if ($junitDirectory) {
            New-Item -ItemType Directory -Path $junitDirectory -Force | Out-Null
        }
        $arguments += @('--junit', $junit)
    }

    Write-Host "Testing $current" -ForegroundColor Cyan
    & $runner @arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host 'All tests passed.' -ForegroundColor Green
exit 0

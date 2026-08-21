#Requires -Version 7.0
<#
.SYNOPSIS
    Builds dhepz. This is the only supported way to build, and it is what CI runs.

.DESCRIPTION
    Finds MSBuild through vswhere rather than assuming a path, and passes the
    toolchain pins explicitly on the command line as well as in the project
    file. Belt and braces: an MSBuild invoked from a different developer
    command prompt would otherwise pick up whatever toolset that prompt was
    configured for.

    CI runs this same script. "Works on my machine" and "works in CI" cannot
    diverge if there is only one code path.

.EXAMPLE
    ./tools/Build.ps1
    Debug x64.

.EXAMPLE
    ./tools/Build.ps1 -Configuration Release -Version 0.2.0
    Release x64 with an overridden version, as the release workflow does.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    # x64 only. ARM64 would need its own measured baseline and its own Velopack
    # runtime; adding it is a deliberate decision, not a flag flip.
    [ValidateSet('x64')]
    [string] $Platform = 'x64',

    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $Version,

    # Force a full rebuild instead of an incremental build.
    [switch] $Rebuild,

    # Skip dependency restore and the toolchain check. For a fast inner loop
    # only — never in CI, where the checks are the point.
    [switch] $SkipChecks
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

if (-not $SkipChecks) {
    & (Join-Path $PSScriptRoot 'Test-Toolchain.ps1') -SkipDotnet
    if ($LASTEXITCODE -ne 0) {
        throw 'Toolchain verification failed. See the problems listed above.'
    }

    & (Join-Path $PSScriptRoot 'Restore-Dependencies.ps1')
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "vswhere.exe not found at '$vswhere'. Install Visual Studio 2022 C++ Build Tools."
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
    -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1

if (-not $msbuild) {
    throw 'MSBuild for Visual Studio 2022 was not found. Install the Microsoft.Component.MSBuild component.'
}

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }

$arguments = @(
    (Join-Path $repositoryRoot 'dhepz.sln'),
    '/m', '/nologo', '/v:minimal', "/t:$target",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    '/p:PlatformToolset=v143',
    '/p:VCToolsVersion=14.44.35207',
    '/p:WindowsTargetPlatformVersion=10.0.26100.0',
    '/p:PreferredToolArchitecture=x64'
)

if ($PSBoundParameters.ContainsKey('Version')) {
    # The release workflow overrides the version without editing version.props,
    # so a tag build does not need a commit. Keep the property names in sync
    # with version.props.
    $parsed = [Version]::Parse($Version)
    $arguments += @(
        "/p:DhepzVersion=$Version",
        "/p:DhepzVersionMajor=$($parsed.Major)",
        "/p:DhepzVersionMinor=$($parsed.Minor)",
        "/p:DhepzVersionPatch=$($parsed.Build)",
        '/p:DhepzVersionBuild=0'
    )
}

Write-Host "Building $Configuration|$Platform" -ForegroundColor Cyan
& $msbuild @arguments
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    $exe = Join-Path $repositoryRoot "build\$Platform\$Configuration\dhepz.exe"
    if (Test-Path -LiteralPath $exe -PathType Leaf) {
        $size = [math]::Round((Get-Item -LiteralPath $exe).Length / 1KB, 1)
        Write-Host "Built $exe ($size KB)" -ForegroundColor Green
    }
}

exit $exitCode

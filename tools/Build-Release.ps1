param(
    [string] $Version = (& (Join-Path $PSScriptRoot 'Get-ProjectVersion.ps1')),
    [string] $PublishDirectory = (Join-Path $PSScriptRoot '..\artifacts\publish'),
    [string] $OutputDirectory = (Join-Path $PSScriptRoot '..\Releases'),
    [string] $ReleaseNotes
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw 'Version harus menggunakan semver tiga bagian, contoh 0.1.0.'
}

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repositoryPrefix = $repositoryRoot.TrimEnd('\') + '\'
$publishPath = [System.IO.Path]::GetFullPath($PublishDirectory)
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
foreach ($path in @($publishPath, $outputPath)) {
    if (-not $path.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Output harus berada di dalam repository: $path"
    }
}

if (Test-Path -LiteralPath $publishPath) {
    Remove-Item -LiteralPath $publishPath -Recurse -Force
}
New-Item -ItemType Directory -Path $publishPath -Force | Out-Null
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

& (Join-Path $PSScriptRoot 'Build.ps1') -Configuration Release -Version $Version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$releaseBuild = Join-Path $repositoryRoot 'build\x64\Release'
foreach ($name in @('dhepz.exe', 'velopack_libc.dll')) {
    $source = Join-Path $releaseBuild $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "File paket tidak ditemukan: $source"
    }
    Copy-Item -LiteralPath $source -Destination $publishPath -Force
}

& dotnet tool restore
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$packArguments = @(
    'vpk', 'pack',
    # Keep the replaceable install tree separate from
    # %LOCALAPPDATA%\dhepz\state, exactly as Open-terminal V1 separates its
    # package id from its state directory.
    '--packId', 'dhepzLauncher',
    '--packVersion', $Version,
    '--packDir', $publishPath,
    '--mainExe', 'dhepz.exe',
    '--packTitle', 'dhepz',
    '--packAuthors', 'yuzhayo',
    '--icon', (Join-Path $repositoryRoot 'assets\app.ico'),
    '--outputDir', $outputPath,
    '--runtime', 'win-x64',
    '--shortcuts', 'Desktop,StartMenuRoot'
)
if (-not [string]::IsNullOrWhiteSpace($ReleaseNotes)) {
    $notesPath = [System.IO.Path]::GetFullPath($ReleaseNotes)
    if (-not (Test-Path -LiteralPath $notesPath -PathType Leaf)) {
        throw "Release notes tidak ditemukan: $notesPath"
    }
    $packArguments += @('--releaseNotes', $notesPath)
}

& dotnet @packArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$setup = Get-ChildItem -LiteralPath $outputPath -Filter '*-Setup.exe' |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $setup) {
    throw 'Velopack tidak menghasilkan installer Setup.exe.'
}

[pscustomobject]@{
    Version = $Version
    Setup = $setup.FullName
    Sha256 = (Get-FileHash -LiteralPath $setup.FullName -Algorithm SHA256).Hash
} | ConvertTo-Json

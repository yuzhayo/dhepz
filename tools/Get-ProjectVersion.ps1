param(
    [string] $Project = (Join-Path $PSScriptRoot '..\version.props')
)

$ErrorActionPreference = 'Stop'
$content = Get-Content -LiteralPath $Project -Raw
$match = [regex]::Match($content, '<DhepzVersion>(?<version>\d+\.\d+\.\d+)</DhepzVersion>')
if (-not $match.Success) {
    throw "DhepzVersion semver tiga bagian tidak ditemukan di $Project."
}

$match.Groups['version'].Value

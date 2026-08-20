#Requires -Version 5.1
<#
Creates a new UI screen file under assets/ui/screens from the current
schema. Adding a screen is one JSON file (G3): this script writes it,
round-trips it through ConvertFrom-Json, writes UTF-8 without BOM, refuses
to overwrite, and then runs the build-time validation so a broken screen
never reaches main.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidatePattern('^[a-z0-9]+(-[a-z0-9]+)*$')]
    [string] $RouteId,

    [string] $Title,

    [string] $RepositoryRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $PSBoundParameters.ContainsKey('RepositoryRoot')) {
    $RepositoryRoot = Split-Path -Parent $PSScriptRoot
}
$RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot)

$screensRoot = Join-Path $RepositoryRoot 'assets\ui\screens'
$null = New-Item -ItemType Directory -Force -Path $screensRoot

if (-not $PSBoundParameters.ContainsKey('Title') -or [string]::IsNullOrWhiteSpace($Title)) {
    $words = foreach ($word in $RouteId.Split('-')) {
        if ($word.Length -eq 1) {
            $word.ToUpperInvariant()
        } else {
            $word.Substring(0, 1).ToUpperInvariant() + $word.Substring(1)
        }
    }
    $Title = $words -join ' '
}

$screenPath = Join-Path $screensRoot "$RouteId.json"
if (Test-Path -LiteralPath $screenPath) {
    throw "Screen '$RouteId' already exists: $screenPath"
}

$screen = [ordered]@{
    components = @(
        [ordered]@{
            type = 'screen'
            route_id = $RouteId
            tab_label = $Title
            show_in_tabs = $true
            children = @(
                [ordered]@{
                    type = 'container'
                    direction = 'column'
                    gap = 12
                    padding = [ordered]@{ left = 24; top = 24; right = 24; bottom = 24 }
                    children = @(
                        [ordered]@{
                            type = 'text'
                            text = $Title
                            variant = 'title'
                        }
                    )
                }
            )
        }
    )
}

$json = ($screen | ConvertTo-Json -Depth 32) + "`n"
$null = $json | ConvertFrom-Json   # round-trip: what we wrote must parse
$encoding = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($screenPath, $json, $encoding)

& (Join-Path $PSScriptRoot 'Merge-UiConfig.ps1') -RepositoryRoot $RepositoryRoot
if ($LASTEXITCODE -ne 0) {
    throw "Screen written, but validation failed. Fix the diagnostics above."
}

Write-Host "Screen created: $screenPath"

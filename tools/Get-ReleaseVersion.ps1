param(
    [ValidateSet('patch', 'minor', 'major')]
    [string] $Bump = 'patch',
    [string] $Project = (Join-Path $PSScriptRoot '..\version.props')
)

$ErrorActionPreference = 'Stop'
$projectVersion = [Version]::Parse((& (Join-Path $PSScriptRoot 'Get-ProjectVersion.ps1') -Project $Project))
$tagVersions = @(git tag --list 'v*' | ForEach-Object {
    $parsed = [Version]::new()
    if ([Version]::TryParse($_.TrimStart('v'), [ref] $parsed) -and $parsed.Revision -le 0) {
        $parsed
    }
})

if ($tagVersions.Count -eq 0) {
    "$($projectVersion.Major).$($projectVersion.Minor).$($projectVersion.Build)"
    exit 0
}

$latest = $tagVersions | Sort-Object -Descending | Select-Object -First 1
if ($projectVersion -gt $latest) {
    "$($projectVersion.Major).$($projectVersion.Minor).$($projectVersion.Build)"
    exit 0
}

$next = switch ($Bump) {
    'patch' { [Version]::new($latest.Major, $latest.Minor, $latest.Build + 1) }
    'minor' { [Version]::new($latest.Major, $latest.Minor + 1, 0) }
    'major' { [Version]::new($latest.Major + 1, 0, 0) }
}
"$($next.Major).$($next.Minor).$($next.Build)"

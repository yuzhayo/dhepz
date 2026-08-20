#Requires -Version 5.1
<#
The build-time merge/validation step (#57): resolves the core catalog plus
every screen in assets/ui/screens through the same resolver the app uses at
runtime, and prints file(line,column) diagnostics. Embedded sources merge
first, user override last (embedded < override); at build time we validate
the embedded set.

-SelfTest proves the CI gate: a deliberately malformed screen in a temp dir
must fail with a line number in the output.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',

    [switch] $SelfTest,

    [string] $RepositoryRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $PSBoundParameters.ContainsKey('RepositoryRoot')) {
    $RepositoryRoot = Split-Path -Parent $PSScriptRoot
}
$RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot)

$exe = Join-Path $RepositoryRoot "build\x64\$Configuration\dhepz.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "dhepz.exe not found for $Configuration — build first: $exe"
}
$core = Join-Path $RepositoryRoot 'assets\ui\core.json'
$screens = Join-Path $RepositoryRoot 'assets\ui\screens'

# GUI-subsystem exe: $LASTEXITCODE is never set for it, so wait explicitly.
$process = Start-Process -FilePath $exe -ArgumentList '--validate-ui', $core, $screens -Wait -PassThru -NoNewWindow
if ($process.ExitCode -ne 0) {
    exit 1
}

if ($SelfTest) {
    $temp = Join-Path ([System.IO.Path]::GetTempPath()) ("dhepz-uival-" + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $temp
    try {
        # Line 3 carries the bad property; the diagnostic must name it.
        $bad = "{`n  `"components`": [`n    { `"type`": `"screen`", `"route_id`": `"x`", `"rout`": 1 }`n  ]`n}`n"
        [System.IO.File]::WriteAllText((Join-Path $temp 'bad.json'), $bad, (New-Object System.Text.UTF8Encoding($false)))
        $outFile = Join-Path $temp 'out.txt'
        $process = Start-Process -FilePath $exe -ArgumentList '--validate-ui', $core, $temp -Wait -PassThru -NoNewWindow -RedirectStandardOutput $outFile
        $output = Get-Content -LiteralPath $outFile -Raw
        if ($process.ExitCode -eq 0) {
            throw "SelfTest: malformed screen was accepted"
        }
        if ($output -notmatch '\(3,') {
            throw "SelfTest: diagnostics did not carry the failing line: $output"
        }
        Write-Host 'SelfTest: malformed screen rejected with file+line.'
    } finally {
        Remove-Item -Recurse -Force -LiteralPath $temp
    }
}

exit 0

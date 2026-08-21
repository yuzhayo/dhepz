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

    [switch] $GenerateEmbedded,

    [string] $RepositoryRoot,

    [string] $EmbeddedOutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $PSBoundParameters.ContainsKey('RepositoryRoot')) {
    $RepositoryRoot = Split-Path -Parent $PSScriptRoot
}
$RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot)

if ($GenerateEmbedded) {
    # Build-time UI half (plan Part 3, scan stage 1): one embedded document,
    # no filesystem walk at startup. PowerShell-only so a clean build can run
    # it before the exe exists; full schema validation runs at test time and
    # at the gate.
    $componentCount = 0
    $sources = New-Object System.Collections.ArrayList
    $manifests = New-Object System.Collections.ArrayList
    $screensRoot = Join-Path $RepositoryRoot 'assets\ui\screens'
    if (Test-Path -LiteralPath $screensRoot -PathType Container) {
        foreach ($file in @(Get-ChildItem -LiteralPath $screensRoot -Filter *.json | Sort-Object Name)) {
            $text = Get-Content -LiteralPath $file.FullName -Raw
            try { $doc = $text | ConvertFrom-Json -ErrorAction Stop }
            catch { throw "$($file.FullName): malformed JSON: $($_.Exception.Message)" }
            foreach ($component in @($doc.components)) { $componentCount++ }
            $relative = [IO.Path]::GetRelativePath($RepositoryRoot, $file.FullName).Replace('\', '/')
            $null = $sources.Add([ordered]@{ file = $relative; text = $text })
        }
    }
    $modulesRoot = Join-Path $RepositoryRoot 'src\modules'
    if (Test-Path -LiteralPath $modulesRoot -PathType Container) {
        foreach ($dir in @(Get-ChildItem -LiteralPath $modulesRoot -Directory | Sort-Object Name)) {
            $manifestPath = Join-Path $dir.FullName 'module.json'
            $screenPath = Join-Path $dir.FullName 'screen.json'
            $hasManifest = Test-Path -LiteralPath $manifestPath
            $hasScreen = Test-Path -LiteralPath $screenPath
            # Code-only folders (the contract, the registry) are not modules.
            if (-not $hasManifest -and -not $hasScreen) { continue }
            if (-not $hasManifest) {
                throw "Module folder '$($dir.Name)' has no module.json — skipped folders are only tolerated at runtime, never in src\modules at build time."
            }
            if (-not $hasScreen) {
                throw "Module '$($dir.Name)' has no screen.json."
            }
            $screenText = Get-Content -LiteralPath $screenPath -Raw
            try { $screenDoc = $screenText | ConvertFrom-Json -ErrorAction Stop }
            catch { throw "${screenPath}: malformed JSON: $($_.Exception.Message)" }
            foreach ($component in @($screenDoc.components)) { $componentCount++ }
            $screenRelative = [IO.Path]::GetRelativePath($RepositoryRoot, $screenPath).Replace('\', '/')
            $null = $sources.Add([ordered]@{ file = $screenRelative; text = $screenText })

            $manifestText = Get-Content -LiteralPath $manifestPath -Raw
            try { $null = $manifestText | ConvertFrom-Json -ErrorAction Stop }
            catch { throw "${manifestPath}: malformed JSON: $($_.Exception.Message)" }
            $manifestRelative = [IO.Path]::GetRelativePath($RepositoryRoot, $manifestPath).Replace('\', '/')
            $null = $manifests.Add([ordered]@{ file = $manifestRelative; text = $manifestText })
        }
    }
    $corePath = Join-Path $RepositoryRoot 'assets\ui\core.json'
    $coreText = Get-Content -LiteralPath $corePath -Raw
    try { $core = $coreText | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "${corePath}: malformed JSON: $($_.Exception.Message)" }
    $merged = [ordered]@{
        core = $core
        coreText = $coreText
        coreFile = 'assets/ui/core.json'
        sources = $sources
        modules = $manifests
    }
    if (-not $PSBoundParameters.ContainsKey('EmbeddedOutputPath')) {
        $EmbeddedOutputPath = Join-Path $RepositoryRoot 'build\generated\embedded_ui.json'
    }
    $EmbeddedOutputPath = [System.IO.Path]::GetFullPath($EmbeddedOutputPath)
    $outDir = Split-Path -Parent $EmbeddedOutputPath
    $null = New-Item -ItemType Directory -Force -Path $outDir
    $json = ($merged | ConvertTo-Json -Depth 32) + "`n"
    [System.IO.File]::WriteAllText($EmbeddedOutputPath, $json, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Embedded UI document written ($componentCount components)."
    exit 0
}

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

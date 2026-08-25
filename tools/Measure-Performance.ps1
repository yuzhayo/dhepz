#Requires -Version 7.0
<#
.SYNOPSIS
    Records the Phase 0 baseline: cold start and idle footprint of the
    tray-resident process, from a deployed Release build.

.DESCRIPTION
    The measurement rule is that numbers come from an installed binary,
    never the build tree — a build-tree EXE has different paging behaviour,
    so its numbers do not predict the shipped app. The Velopack installer
    does not exist until Phase 6 (#28), so for Phase 0 this script deploys
    the Release EXE to %LOCALAPPDATA%\dhepz\baseline\ and measures that
    copy. Passing a build-tree path directly is refused.

    Cold start is measured as process launch (Start-Process) -> the
    infrastructure window class existing. Phase 0 has no visible window; the
    infrastructure window is the last step before the tray icon is added, so
    it is the honest "startup complete" marker. The visible-window metrics
    return with Phase 1.

    The toolchain pin is verified before any measurement, so a number can
    never be attributed to the wrong compiler.

.EXAMPLE
    ./tools/Measure-Performance.ps1
    24 cold-start runs plus the 60 s idle sample.

.EXAMPLE
    ./tools/Measure-Performance.ps1 -Runs 40 -IdleSeconds 120
#>
[CmdletBinding()]
param(
    # Cold-start sample count. The issue floor is 20.
    [ValidateRange(20, 1000)]
    [int] $Runs = 24,

    # How long the idle sample watches CPU and private bytes.
    [ValidateRange(10, 3600)]
    [int] $IdleSeconds = 60,

    # Inner loop only. CI-style discipline is the default: pinned
    # toolchain first, or no numbers at all.
    [switch] $SkipChecks
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$className = 'dhepz.InfrastructureWindow'
$installDir = Join-Path $env:LOCALAPPDATA 'dhepz\baseline'
$installedExe = Join-Path $installDir 'dhepz.exe'
$buildTreeExe = Join-Path $repositoryRoot 'build\x64\Release\dhepz.exe'
$buildTreeRuntime = Join-Path $repositoryRoot 'build\x64\Release\velopack_libc.dll'
$installedRuntime = Join-Path $installDir 'velopack_libc.dll'

if (-not $SkipChecks) {
    & (Join-Path $PSScriptRoot 'Test-Toolchain.ps1') -SkipDotnet
    if ($LASTEXITCODE -ne 0) {
        throw 'Toolchain verification failed. Numbers are only meaningful on the pinned toolchain.'
    }
}

# --- Deploy ---------------------------------------------------------------

if (-not (Test-Path -LiteralPath $buildTreeExe -PathType Leaf) -or
    -not (Test-Path -LiteralPath $buildTreeRuntime -PathType Leaf)) {
    throw "Release output is incomplete. Expected '$buildTreeExe' and '$buildTreeRuntime'. Run tools\Build.ps1 -Configuration Release first."
}
New-Item -ItemType Directory -Path $installDir -Force | Out-Null
Copy-Item -LiteralPath $buildTreeExe -Destination $installedExe -Force
Copy-Item -LiteralPath $buildTreeRuntime -Destination $installedRuntime -Force
Write-Host "Deployed $installedExe and $installedRuntime" -ForegroundColor Cyan

# --- Native helpers ---------------------------------------------------------

Add-Type -Namespace Dhepz.Perf -Name Native -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern bool QueryPerformanceCounter(out long value);
[DllImport("kernel32.dll")] public static extern bool QueryPerformanceFrequency(out long value);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string className, string windowName);
[DllImport("kernel32.dll", SetLastError = true)] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int processId);
[DllImport("user32.dll")] public static extern uint GetGuiResources(System.IntPtr process, uint flags);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr handle);
'@

function Get-Qpc {
    [long] $value = 0
    [void][Dhepz.Perf.Native]::QueryPerformanceCounter([ref]$value)
    return $value
}

$qpcFrequency = [long] 0
[void][Dhepz.Perf.Native]::QueryPerformanceFrequency([ref]$qpcFrequency)

function Get-Milliseconds([long] $start, [long] $end) {
    return ($end - $start) * 1000.0 / $qpcFrequency
}

# --- Cold start -----------------------------------------------------------

Write-Host "Measuring cold start over $Runs runs..." -ForegroundColor Cyan
$samples = @()
$failures = 0

for ($i = 1; $i -le $Runs; $i++) {
    $start = Get-Qpc
    $process = Start-Process -FilePath $installedExe -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([Dhepz.Perf.Native]::FindWindowW($className, $null) -eq [System.IntPtr]::Zero) {
        if ([DateTime]::UtcNow -gt $deadline) { break }
        Start-Sleep -Milliseconds 1
    }
    $end = Get-Qpc
    $found = [Dhepz.Perf.Native]::FindWindowW($className, $null) -ne [System.IntPtr]::Zero
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue

    if (-not $found) {
        Write-Host "  run ${i}: FAILED (no infrastructure window within 10 s)" -ForegroundColor Red
        $failures++
    } else {
        $ms = [math]::Round((Get-Milliseconds $start $end), 2)
        $samples += $ms
        Write-Host "  run ${i}: $ms ms"
    }
    # Let the shell catch up before the next launch; killed instances leave
    # tray zombies that explorer sweeps on hover, which is harmless here.
    Start-Sleep -Milliseconds 1200
}

if ($samples.Count -lt 20) {
    throw "Only $($samples.Count) successful runs — not enough for a baseline."
}

$sorted = $samples | Sort-Object
function Get-Percentile([double[]] $values, [double] $p) {
    # Nearest-rank. Good enough for n >= 20 and matches how the budgets read.
    $rank = [math]::Ceiling($p / 100.0 * $values.Count)
    return $values[[math]::Max(0, $rank - 1)]
}
$coldP50 = Get-Percentile $sorted 50
$coldP95 = Get-Percentile $sorted 95
$coldMin = $sorted[0]
$coldMax = $sorted[$sorted.Count - 1]

# --- Idle footprint ---------------------------------------------------------

Write-Host "Measuring idle footprint for $IdleSeconds s..." -ForegroundColor Cyan
$process = Start-Process -FilePath $installedExe -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds(10)
while ([Dhepz.Perf.Native]::FindWindowW($className, $null) -eq [System.IntPtr]::Zero) {
    if ([DateTime]::UtcNow -gt $deadline) { throw 'Idle sample: infrastructure window never appeared.' }
    Start-Sleep -Milliseconds 2
}
# Give the startup burst time to settle before sampling.
Start-Sleep -Seconds 10

$proc = Get-Process -Id $process.Id
$handle = [Dhepz.Perf.Native]::OpenProcess(0x0400, $false, $process.Id)  # PROCESS_QUERY_INFORMATION
if ($handle -eq [System.IntPtr]::Zero) { throw 'Could not open the process for GDI/USER counters.' }
$gdiAtStart = [Dhepz.Perf.Native]::GetGuiResources($handle, 0)
$userAtStart = [Dhepz.Perf.Native]::GetGuiResources($handle, 1)
$threadsAtStart = $proc.Threads.Count
$privateAtStart = $proc.PrivateMemorySize64
$cpuAtStart = $proc.TotalProcessorTime
$sampleStart = Get-Qpc

Start-Sleep -Seconds $IdleSeconds

$proc.Refresh()
$cpuAtEnd = $proc.TotalProcessorTime
$sampleEnd = Get-Qpc
$elapsedSeconds = (Get-Milliseconds $sampleStart $sampleEnd) / 1000.0
$idleCpuPercent = ($cpuAtEnd - $cpuAtStart).TotalMilliseconds / ($elapsedSeconds * 1000.0) * 100.0 / [Environment]::ProcessorCount
$privateAtEnd = $proc.PrivateMemorySize64
$gdiAtEnd = [Dhepz.Perf.Native]::GetGuiResources($handle, 0)
$userAtEnd = [Dhepz.Perf.Native]::GetGuiResources($handle, 1)
[void][Dhepz.Perf.Native]::CloseHandle($handle)
Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue

# --- Machine and toolchain ----------------------------------------------------

$nt = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$cpuModel = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim()
$ramGb = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsVersion = (& $vswhere -latest -products * -format value -property installationVersion 2>$null)
$installation = & $vswhere -latest -products * -format json | ConvertFrom-Json | Select-Object -First 1
$compiler = Join-Path $installation.installationPath 'VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe'
$clVersion = if (Test-Path -LiteralPath $compiler) { (Get-Item -LiteralPath $compiler).VersionInfo.FileVersion } else { '(not found)' }

# --- Report -------------------------------------------------------------------

$report = [ordered]@{
    recordedUtc       = (Get-Date).ToUniversalTime().ToString('o')
    issue             = 13
    exe               = $installedExe
    machine           = [ordered]@{
        host        = $env:COMPUTERNAME
        cpu         = $cpuModel
        cores       = [Environment]::ProcessorCount
        ramGb       = $ramGb
        windows     = "$($nt.CurrentBuild).$($nt.UBR) ($($nt.DisplayVersion))"
    }
    toolchain         = [ordered]@{
        visualStudio = $vsVersion
        vcTools      = '14.51.36231'
        cl           = $clVersion
        windowsSdk   = '10.0.26100.0'
    }
    coldStartMs       = [ordered]@{
        runs     = $samples.Count
        failures = $failures
        p50      = $coldP50
        p95      = $coldP95
        min      = $coldMin
        max      = $coldMax
        samples  = @($sorted)
    }
    idle              = [ordered]@{
        threadsAtStart    = $threadsAtStart
        privateBytesStart = $privateAtStart
        privateBytesEnd   = $privateAtEnd
        privateDriftBytes = ($privateAtEnd - $privateAtStart)
        gdiObjects        = "$gdiAtStart -> $gdiAtEnd"
        userObjects       = "$userAtStart -> $userAtEnd"
        cpuPercent        = [math]::Round($idleCpuPercent, 4)
        seconds           = [math]::Round($elapsedSeconds, 1)
    }
}

$json = $report | ConvertTo-Json -Depth 6
New-Item -ItemType Directory -Path (Join-Path $repositoryRoot 'artifacts') -Force | Out-Null
$json | Set-Content -LiteralPath (Join-Path $repositoryRoot 'artifacts\baseline.json') -Encoding utf8

Write-Host ''
Write-Host '=== Phase 0 baseline ===' -ForegroundColor Green
Write-Host "Cold start ($($samples.Count) runs, launch -> infrastructure window):"
Write-Host "  p50 = $coldP50 ms   p95 = $coldP95 ms   min = $coldMin ms   max = $coldMax ms   failures = $failures"
Write-Host "Idle over $($report.idle.seconds) s:"
Write-Host "  CPU = $($report.idle.cpuPercent)%   threads = $threadsAtStart"
Write-Host "  private bytes = $privateAtStart -> $privateAtEnd (drift $($report.idle.privateDriftBytes) B)"
Write-Host "  GDI objects = $($report.idle.gdiObjects)   USER objects = $($report.idle.userObjects)"
Write-Host "Machine: $env:COMPUTERNAME / $cpuModel / $ramGb GB / Windows $($report.machine.windows)"
Write-Host "Toolchain: VS $vsVersion, VC 14.51.36231, cl $clVersion, SDK 10.0.26100.0"
Write-Host "Report: artifacts\baseline.json" -ForegroundColor Cyan

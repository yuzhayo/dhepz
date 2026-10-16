$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$versionFile = Join-Path $repositoryRoot 'version.props'
$releaseDirectory = Join-Path $repositoryRoot 'Releases'
$buildScript = Join-Path $PSScriptRoot 'Build-Release.ps1'
$publishExe = Join-Path $repositoryRoot 'artifacts\publish\dhepz.exe'

try {
    Get-Process 'dhepz' -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500

    $content = [System.IO.File]::ReadAllText($versionFile)
    $match = [regex]::Match(
        $content,
        '<DhepzVersion>(\d+)\.(\d+)\.(\d+)</DhepzVersion>')
    if (-not $match.Success) {
        throw 'DhepzVersion tiga bagian tidak ditemukan di version.props.'
    }

    $highestVersion = [version]::new(
        [int]$match.Groups[1].Value,
        [int]$match.Groups[2].Value,
        [int]$match.Groups[3].Value)

    # Tag v* lokal adalah paruh kebenaran yang lain. release.yml menghitung
    # versi dari props + tag dan tidak menulis balik version.props, jadi file
    # itu dengan sengaja tertinggal di belakang tag terbaru. Tanpa membaca tag
    # di sini, build lokal bisa mendarat di versi yang sudah dirilis dan
    # packer menolaknya.
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $tagVersions = @(git tag --list 'v*' 2>$null | ForEach-Object {
            $parsed = [version]::new()
            if ([version]::TryParse($_.TrimStart('v'), [ref] $parsed) -and $parsed.Revision -le 0) {
                $parsed
            }
        })
        foreach ($tagVersion in $tagVersions) {
            if ($tagVersion -gt $highestVersion) {
                $highestVersion = $tagVersion
            }
        }
    }

    # Nupkg lokal yang belum di-tag juga menghitung, supaya tidak ada dua
    # paket dengan versi yang sama.
    if (Test-Path -LiteralPath $releaseDirectory -PathType Container) {
        foreach ($package in Get-ChildItem -LiteralPath $releaseDirectory -File -Filter '*-full.nupkg') {
            if ($package.Name -notmatch '^dhepzLauncher-(\d+\.\d+\.\d+)-full\.nupkg$') {
                continue
            }

            $packageVersion = [version]$Matches[1]
            if ($packageVersion -gt $highestVersion) {
                $highestVersion = $packageVersion
            }
        }
    }

    $next = [version]::new(
        $highestVersion.Major,
        $highestVersion.Minor,
        $highestVersion.Build + 1)
    $nextVersion = $next.ToString(3)

    # Semua lima field di version.props ikut maju: DhepzVersion plus
    # Major/Minor/Patch/Build yang dikonsumsi rc dan manifest. Ditulis hanya
    # setelah build terbukti menghasilkan paket untuk versi ini, jadi build
    # yang gagal tidak memindahkan pointer.
    $replacements = @(
        @{ Pattern = '<DhepzVersion>\d+\.\d+\.\d+</DhepzVersion>'; Value = "<DhepzVersion>$nextVersion</DhepzVersion>" }
        @{ Pattern = '<DhepzVersionMajor>\d+</DhepzVersionMajor>'; Value = "<DhepzVersionMajor>$($next.Major)</DhepzVersionMajor>" }
        @{ Pattern = '<DhepzVersionMinor>\d+</DhepzVersionMinor>'; Value = "<DhepzVersionMinor>$($next.Minor)</DhepzVersionMinor>" }
        @{ Pattern = '<DhepzVersionPatch>\d+</DhepzVersionPatch>'; Value = "<DhepzVersionPatch>$($next.Build)</DhepzVersionPatch>" }
        @{ Pattern = '<DhepzVersionBuild>\d+</DhepzVersionBuild>'; Value = '<DhepzVersionBuild>0</DhepzVersionBuild>' }
    )
    $updated = $content
    foreach ($replacement in $replacements) {
        $updated = [regex]::Replace($updated, $replacement.Pattern, $replacement.Value, 1)
        if (-not $updated.Contains($replacement.Value)) {
            throw "Field tidak ditemukan di version.props: $($replacement.Value)"
        }
    }

    Write-Host "[dhepz] Building release $nextVersion..."
    $buildStartedUtc = [DateTime]::UtcNow

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Command pwsh.exe -ErrorAction Stop).Source
    $startInfo.UseShellExecute = $false
    $startInfo.WorkingDirectory = $repositoryRoot
    foreach ($argument in @(
        '-NoLogo',
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $buildScript,
        '-Version', $nextVersion)) {
        [void]$startInfo.ArgumentList.Add($argument)
    }

    $buildProcess = [System.Diagnostics.Process]::Start($startInfo)
    $buildProcess.WaitForExit()
    if ($buildProcess.ExitCode -ne 0) {
        throw "Build release gagal dengan exit code $($buildProcess.ExitCode)."
    }

    $fullPackage = Join-Path $releaseDirectory "dhepzLauncher-$nextVersion-full.nupkg"
    $setup = Join-Path $releaseDirectory 'dhepzLauncher-win-Setup.exe'
    foreach ($artifact in @($fullPackage, $setup, $publishExe)) {
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
            throw "Artefak build tidak ditemukan: $artifact"
        }
        if ((Get-Item -LiteralPath $artifact).LastWriteTimeUtc -lt $buildStartedUtc.AddSeconds(-1)) {
            throw "Artefak build tidak diperbarui untuk $nextVersion`: $artifact"
        }
    }

    $productVersion = (Get-Item -LiteralPath $publishExe).VersionInfo.ProductVersion
    if (-not $productVersion.StartsWith($nextVersion, [StringComparison]::Ordinal)) {
        throw "Versi executable '$productVersion' tidak sesuai target '$nextVersion'."
    }

    [System.IO.File]::WriteAllText(
        $versionFile,
        $updated,
        [System.Text.UTF8Encoding]::new($false))

    $application = Start-Process -FilePath $publishExe -PassThru
    Write-Host "[dhepz] Version saved: $nextVersion"
    Write-Host "[dhepz] Running PID $($application.Id): $publishExe"
}
catch {
    Write-Error $_
    exit 1
}

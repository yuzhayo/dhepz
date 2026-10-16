param(
    [ValidateSet('', 'patch', 'minor', 'major')]
    [string] $Bump = 'patch'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($Bump)) {
    $Bump = 'patch'
}

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $repositoryRoot

function Invoke-Native([string] $FileName, [string[]] $Arguments) {
    & $FileName @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FileName gagal dengan exit code $LASTEXITCODE."
    }
}

function Wait-ForWorkflowRun(
    [string] $Workflow,
    [string] $Commit,
    [string] $Event,
    [DateTime] $NotBeforeUtc) {
    $deadline = [DateTime]::UtcNow.AddMinutes(2)
    do {
        $json = gh run list `
            --workflow $Workflow `
            --branch main `
            --commit $Commit `
            --event $Event `
            --limit 10 `
            --json databaseId,createdAt,headSha
        if ($LASTEXITCODE -ne 0) {
            throw "Tidak dapat membaca workflow $Workflow."
        }

        $run = @($json | ConvertFrom-Json) |
            Where-Object {
                $_.headSha -eq $Commit -and
                ([DateTime]$_.createdAt).ToUniversalTime() -ge $NotBeforeUtc.AddSeconds(-5)
            } |
            Sort-Object { [DateTime]$_.createdAt } -Descending |
            Select-Object -First 1
        if ($run) {
            return $run
        }

        Start-Sleep -Seconds 3
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "Workflow $Workflow untuk commit $Commit tidak muncul dalam 2 menit."
}

try {
    if ((git branch --show-current) -ne 'main') {
        throw 'Release hanya dapat dijalankan dari branch main.'
    }

    Invoke-Native gh @('auth', 'status')
    Invoke-Native git @('fetch', 'origin', 'main', '--tags')

    & git diff --quiet
    if ($LASTEXITCODE -ne 0) {
        throw 'Ada perubahan tracked yang belum di-commit. Commit atau stash dahulu.'
    }
    & git diff --cached --quiet
    if ($LASTEXITCODE -ne 0) {
        throw 'Ada perubahan staged yang belum di-commit. Commit atau unstage dahulu.'
    }

    $commit = (git rev-parse HEAD).Trim()
    $remoteHead = (git rev-parse origin/main).Trim()
    if ($commit -ne $remoteHead) {
        throw "HEAD lokal ($commit) tidak sama dengan origin/main ($remoteHead). Sinkronkan dahulu."
    }

    # release.yml sendiri mensyaratkan CI hijau untuk commit ini. Tunggu di
    # sini kalau run-nya belum selesai, supaya dispatch tidak jatuh di gate
    # itu dan meninggalkan run merah.
    $ciDeadline = [DateTime]::UtcNow.AddMinutes(2)
    $ciRun = $null
    do {
        $ciRun = @(gh run list --workflow 'ci.yml' --branch main --commit $commit `
            --limit 1 --json databaseId,status,conclusion | ConvertFrom-Json) |
            Select-Object -First 1
        if ($ciRun) { break }
        Start-Sleep -Seconds 3
    } while ([DateTime]::UtcNow -lt $ciDeadline)

    if (-not $ciRun) {
        throw "Run CI untuk commit $commit tidak ditemukan."
    }
    if ($ciRun.status -ne 'completed') {
        Write-Host "[dhepz] Menunggu CI untuk $commit..."
        Invoke-Native gh @('run', 'watch', [string]$ciRun.databaseId, '--exit-status')
    }
    elseif ($ciRun.conclusion -ne 'success') {
        throw "CI untuk commit $commit tidak hijau ($($ciRun.conclusion))."
    }

    $releaseVersion = (& (Join-Path $PSScriptRoot 'Get-ReleaseVersion.ps1') -Bump $Bump).Trim()

    Write-Host "[dhepz] Menjalankan release v$releaseVersion dari commit $commit..."
    $releaseNotBeforeUtc = [DateTime]::UtcNow
    Invoke-Native gh @('workflow', 'run', 'release.yml', '--ref', 'main', '-f', "bump=$Bump")
    $releaseRun = Wait-ForWorkflowRun 'release.yml' $commit 'workflow_dispatch' $releaseNotBeforeUtc
    Invoke-Native gh @('run', 'watch', [string]$releaseRun.databaseId, '--exit-status')

    $release = gh release view "v$releaseVersion" --json tagName,targetCommitish,url | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or $release.tagName -ne "v$releaseVersion" -or $release.targetCommitish -ne $commit) {
        throw "Release v$releaseVersion untuk commit $commit tidak dapat diverifikasi."
    }

    Write-Host "[dhepz] Commit: $commit"
    Write-Host "[dhepz] Release: $($release.url)"
}
catch {
    Write-Error $_
    exit 1
}

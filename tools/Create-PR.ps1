#Requires -Version 7.0
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $repositoryRoot

function Invoke-Checked {
    param(
        [Parameter(Mandatory)]
        [string] $Command,

        [Parameter(ValueFromRemainingArguments)]
        [string[]] $Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $Command $($Arguments -join ' ')"
    }
}

function Read-Trimmed {
    param([Parameter(Mandatory)][string] $Prompt)

    $value = Read-Host $Prompt
    if ($null -eq $value) {
        return ''
    }
    return $value.Trim()
}

function Remove-FailedPullRequest {
    param(
        [Parameter(Mandatory)][int] $Number,
        [Parameter(Mandatory)][string] $Branch,
        [Parameter(Mandatory)][string] $Reason
    )

    Write-Host ''
    Write-Host "$Reason Closing PR #$Number dan menghapus branch..." -ForegroundColor Yellow
    Invoke-Checked -Command 'gh' -Arguments @(
        'pr', 'close', [string]$Number, '--delete-branch'
    )

    $branchAfterClose = (& git branch --show-current).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Gagal membaca branch setelah menutup PR.' }
    if ($branchAfterClose -ne 'main') {
        Invoke-Checked -Command 'git' -Arguments @('switch', 'main')
    }
    Invoke-Checked -Command 'git' -Arguments @('pull', '--ff-only', 'origin', 'main')

    & git show-ref --verify --quiet "refs/heads/$Branch"
    if ($LASTEXITCODE -eq 0) {
        Invoke-Checked -Command 'git' -Arguments @('branch', '-D', $Branch)
    } elseif ($LASTEXITCODE -ne 1) {
        throw "Gagal memeriksa branch lokal '$Branch'."
    }

    throw "$Reason PR ditutup; branch remote dan lokal dihapus."
}

foreach ($command in @('git', 'gh')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "$command tidak ditemukan di PATH."
    }
}

& git rev-parse --is-inside-work-tree *> $null
if ($LASTEXITCODE -ne 0) {
    throw "'$repositoryRoot' bukan Git repository."
}

& gh auth status *> $null
if ($LASTEXITCODE -ne 0) {
    throw 'GitHub CLI belum login. Jalankan: gh auth login'
}

Write-Host 'Updating origin/main...' -ForegroundColor Cyan
Invoke-Checked -Command 'git' -Arguments @('fetch', 'origin', 'main', '--quiet')

$currentBranch = (& git branch --show-current).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($currentBranch)) {
    throw 'Git sedang berada pada detached HEAD.'
}

if ($currentBranch -eq 'main') {
    $changes = @(git status --porcelain)
    if ($changes.Count -eq 0) {
        Write-Host 'Tidak ada perubahan lokal untuk dibuatkan PR.' -ForegroundColor Yellow
        exit 0
    }

    $newBranch = Read-Trimmed 'Nama branch baru, contoh feat/update-settings'
    if ([string]::IsNullOrWhiteSpace($newBranch)) {
        Write-Host 'Dibatalkan: nama branch wajib diisi.' -ForegroundColor Yellow
        exit 0
    }

    & git check-ref-format --branch $newBranch *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "Nama branch tidak valid: $newBranch"
    }

    Invoke-Checked -Command 'git' -Arguments @('switch', '-c', $newBranch)
    $currentBranch = $newBranch
} else {
    $answer = Read-Trimmed "Gunakan branch '$currentBranch'? [Y/n]"
    if ($answer -eq 'n') {
        Write-Host 'Dibatalkan.' -ForegroundColor Yellow
        exit 0
    }
}

$changes = @(git status --porcelain)
if ($changes.Count -gt 0) {
    Write-Host ''
    Write-Host 'File yang akan dimasukkan ke commit:' -ForegroundColor Cyan
    git status --short

    $stageAll = Read-Trimmed 'Stage semua file di atas? [y/N]'
    if ($stageAll -ne 'y') {
        Write-Host 'Dibatalkan sebelum staging.' -ForegroundColor Yellow
        exit 0
    }

    $commitMessage = Read-Trimmed 'Commit message'
    if ([string]::IsNullOrWhiteSpace($commitMessage)) {
        Write-Host 'Dibatalkan: commit message wajib diisi.' -ForegroundColor Yellow
        exit 0
    }

    Invoke-Checked -Command 'git' -Arguments @('add', '--all')

    & git diff --cached --quiet
    if ($LASTEXITCODE -eq 0) {
        Write-Host 'Tidak ada perubahan yang dapat di-commit.' -ForegroundColor Yellow
        exit 0
    }
    if ($LASTEXITCODE -ne 1) {
        throw "Gagal memeriksa staged changes (exit $LASTEXITCODE)."
    }

    Invoke-Checked -Command 'git' -Arguments @('commit', '-m', $commitMessage)
}

$commitCountText = (& git rev-list --count origin/main..HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Gagal membandingkan branch dengan origin/main.'
}
if ([int]$commitCountText -eq 0) {
    Write-Host 'Tidak ada commit pada branch ini yang belum ada di origin/main.' -ForegroundColor Yellow
    exit 0
}

Write-Host ''
Write-Host "Pushing $currentBranch..." -ForegroundColor Cyan
Invoke-Checked -Command 'git' -Arguments @('push', '-u', 'origin', 'HEAD')

$prRows = @((& gh pr list --head $currentBranch --base main --state open --json number,url) |
        ConvertFrom-Json)
if ($LASTEXITCODE -ne 0) { throw 'Gagal memeriksa PR yang sudah ada.' }
$pr = $prRows | Select-Object -First 1

Write-Host ''
if ($pr) {
    Write-Host 'PR sudah tersedia:' -ForegroundColor Green
    Write-Host $pr.url
} else {
    Write-Host 'Creating pull request...' -ForegroundColor Cyan
    Invoke-Checked -Command 'gh' -Arguments @(
        'pr', 'create', '--base', 'main', '--head', $currentBranch, '--fill'
    )

    $prRows = @((& gh pr list --head $currentBranch --base main --state open --json number,url) |
            ConvertFrom-Json)
    if ($LASTEXITCODE -ne 0) { throw 'PR dibuat, tetapi detailnya tidak dapat dibaca.' }
    $pr = $prRows | Select-Object -First 1
    if (-not $pr) { throw 'PR dibuat, tetapi tidak ditemukan sebagai PR terbuka.' }
}

Write-Host ''
Write-Host "Menunggu required checks untuk PR #$($pr.number)..." -ForegroundColor Cyan
$checkRegistrationDeadline = (Get-Date).AddMinutes(10)
do {
    $checkJson = (& gh pr checks ([string]$pr.number) --json name,state,bucket 2>$null |
            Out-String).Trim()
    $checkExitCode = $LASTEXITCODE
    $checks = @(
        if ($checkExitCode -eq 0 -and $checkJson) {
            $checkJson | ConvertFrom-Json
        }
    )

    if ($checks.Count -gt 0) { break }
    if ((Get-Date) -ge $checkRegistrationDeadline) {
        Remove-FailedPullRequest -Number $pr.number -Branch $currentBranch `
            -Reason 'Required checks tidak muncul dalam 10 menit.'
    }

    Write-Host 'Checks belum terdaftar; mencoba lagi dalam 10 detik...'
    Start-Sleep -Seconds 10
} while ($true)

& gh pr checks ([string]$pr.number) --watch --interval 10
if ($LASTEXITCODE -ne 0) {
    Remove-FailedPullRequest -Number $pr.number -Branch $currentBranch `
        -Reason 'CI tidak green.'
}

Write-Host ''
Write-Host "CI green. Merging PR #$($pr.number)..." -ForegroundColor Cyan
Invoke-Checked -Command 'gh' -Arguments @(
    'pr', 'merge', [string]$pr.number, '--merge', '--delete-branch'
)

$branchAfterMerge = (& git branch --show-current).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Gagal membaca branch setelah merge.' }
if ($branchAfterMerge -ne 'main') {
    Invoke-Checked -Command 'git' -Arguments @('switch', 'main')
}
Invoke-Checked -Command 'git' -Arguments @('pull', '--ff-only', 'origin', 'main')

& git show-ref --verify --quiet "refs/heads/$currentBranch"
if ($LASTEXITCODE -eq 0) {
    Invoke-Checked -Command 'git' -Arguments @('branch', '-d', $currentBranch)
} elseif ($LASTEXITCODE -ne 1) {
    throw "Gagal memeriksa branch lokal '$currentBranch'."
}

Write-Host ''
Write-Host 'Selesai: PR merged, main sinkron, branch remote dan lokal dihapus.' -ForegroundColor Green

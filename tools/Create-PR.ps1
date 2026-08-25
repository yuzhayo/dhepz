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

$prUrl = (& gh pr list --head $currentBranch --base main --state open --json url --jq '.[0].url').Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Gagal memeriksa PR yang sudah ada.'
}

Write-Host ''
if ($prUrl) {
    Write-Host 'PR sudah tersedia:' -ForegroundColor Green
    Write-Host $prUrl
} else {
    Write-Host 'Creating pull request...' -ForegroundColor Cyan
    Invoke-Checked -Command 'gh' -Arguments @(
        'pr', 'create', '--base', 'main', '--head', $currentBranch, '--fill'
    )
}

Write-Host ''
Write-Host 'Selesai. PR tidak di-merge otomatis.' -ForegroundColor Green

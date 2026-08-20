#Requires -Version 7.0
<#
.SYNOPSIS
    Restores pinned third-party dependencies into build\deps.

.DESCRIPTION
    Ported from the old build, with nlohmann/json dropped: it existed only to
    serialise IPC JSON, and this project writes its own parser (issue #6).
    Velopack is the only third-party runtime dependency.

    Every download is verified against the sha256 in dependencies.lock.json. A
    version string alone is not enough — a re-tagged GitHub release can change
    the bytes without changing the URL, and a compromised or corrupted archive
    would otherwise be linked straight into the shipped binary.
#>
[CmdletBinding()]
param(
    # Re-download even when the cached copy already verifies.
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cacheRoot = Join-Path $repositoryRoot 'build\deps'
$lockPath = Join-Path $repositoryRoot 'dependencies.lock.json'

if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw "dependencies.lock.json not found at '$lockPath'."
}

$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null

function Get-Sha256 {
    param([Parameter(Mandatory)] [string] $Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-VerifiedFile {
    param(
        [Parameter(Mandatory)] [string] $Url,
        [Parameter(Mandatory)] [string] $Destination,
        [Parameter(Mandatory)] [string] $Sha256,
        [switch] $ForceDownload
    )

    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null

    if (-not $ForceDownload -and (Test-Path -LiteralPath $Destination -PathType Leaf)) {
        if ((Get-Sha256 -Path $Destination) -eq $Sha256) {
            Write-Verbose "Cached and verified: $Destination"
            return
        }
        Write-Host "Cached copy of $(Split-Path -Leaf $Destination) failed verification; re-downloading."
    }

    Write-Host "Downloading $Url"
    # -UseBasicParsing avoids the IE engine dependency on a bare CI runner.
    Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing

    $actual = Get-Sha256 -Path $Destination
    if ($actual -ne $Sha256) {
        # Delete it: leaving an unverified archive in the cache means the next
        # run could pick it up if the check were ever loosened.
        Remove-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
        throw "Checksum mismatch for $Url`n  expected $Sha256`n  actual   $actual`nThe pinned release may have been re-tagged, or the download is corrupt. Do not update the hash without confirming why it changed."
    }
}

$velopack = $lock.velopack
$velopackRoot = Join-Path $cacheRoot "velopack-$($velopack.version)"
$archive = Join-Path $cacheRoot $velopack.asset
$required = @(
    (Join-Path $velopackRoot $velopack.header),
    (Join-Path $velopackRoot $velopack.importLibrary),
    (Join-Path $velopackRoot $velopack.runtime)
)

$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })

# Verify the cached archive even when the extracted tree looks complete.
# Otherwise the hash is checked exactly once, on the cold run, and a later
# tampered or truncated cache is trusted forever. CI is always cold, so this
# only costs a local hash of a 2.6 MB file on a warm tree.
#
# A missing archive with a complete tree also counts as unverifiable: that is
# exactly the state a previous failed run leaves behind, since a mismatched
# download is deleted. Trusting the tree in that state would mean an extracted
# copy that was never checked against the pin.
if (-not $Force -and $missing.Count -eq 0) {
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        Write-Host 'Extracted Velopack tree is present but the archive it came from is gone, so it cannot be verified against the pin. Re-downloading.'
        Remove-Item -LiteralPath $velopackRoot -Recurse -Force -ErrorAction SilentlyContinue
        $missing = $required
    } elseif ((Get-Sha256 -Path $archive) -ne $velopack.sha256) {
        Write-Host 'Cached Velopack archive no longer matches the pinned hash; re-extracting from a fresh download.'
        Remove-Item -LiteralPath $velopackRoot -Recurse -Force -ErrorAction SilentlyContinue
        $missing = $required
    }
}

if ($Force -or $missing.Count -gt 0) {
    Get-VerifiedFile -Url $velopack.url -Destination $archive -Sha256 $velopack.sha256 -ForceDownload:$Force
    New-Item -ItemType Directory -Path $velopackRoot -Force | Out-Null
    Expand-Archive -LiteralPath $archive -DestinationPath $velopackRoot -Force
}

foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "The Velopack archive extracted but is missing an expected file: '$file'. The layout inside velopack_libc_$($velopack.version).zip may have changed; check the paths in dependencies.lock.json."
    }
}

Write-Host "Velopack $($velopack.version) is ready: $velopackRoot" -ForegroundColor Green

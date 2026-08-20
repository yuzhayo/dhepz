# Creates the milestones and labels the issue scripts reference. Idempotent:
# safe to re-run. Run this first when setting up a fresh fork or a new repo.

$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path "$PSScriptRoot\..\..")

$repo = gh repo view --json nameWithOwner -q .nameWithOwner
Write-Host "Repo: $repo"

$milestones = @(
  'Phase 0 - Skeleton and measurement floor',
  'Phase 1 - Window shell and rendering',
  'Phase 2 - Config-driven UI',
  'Phase 3 - The contract and the gate',
  'Phase 4 - Terminal module',
  'Phase 5 - Settings and UI editor',
  'Phase 6 - Resident hardening and shippable installer',
  'Phase 7 - Remaining modules and accessibility'
)

$existing = (gh api "repos/$repo/milestones?state=all" --paginate -q '.[].title')
foreach ($m in $milestones) {
  if ($existing -contains $m) {
    Write-Host "milestone exists: $m"
  } else {
    gh api "repos/$repo/milestones" -f title="$m" | Out-Null
    Write-Host "milestone created: $m"
  }
}

# --force makes label creation idempotent, unlike milestones.
$labels = @(
  @{ n = 'phase-0';               c = '0E8A16'; d = 'Skeleton and measurement floor' },
  @{ n = 'phase-1';               c = '1D76DB'; d = 'Window shell and rendering' },
  @{ n = 'goal:G1-lightweight';   c = 'C5DEF5'; d = 'Idle footprint, zero wakeups, no drift' },
  @{ n = 'goal:G2-responsive';    c = 'D93F0B'; d = 'Responsiveness and visual quality - outranks G1' },
  @{ n = 'goal:G3-json-ui';       c = 'FBCA04'; d = 'UI driven by JSON config' },
  @{ n = 'goal:G4-contract';      c = '5319E7'; d = 'Parent/child module contract' },
  @{ n = 'goal:G5-plug-and-play'; c = '0052CC'; d = 'Drop a folder and it appears' },
  @{ n = 'ported';                c = 'BFDADC'; d = 'Ported from the old Terminal build' },
  @{ n = 'from-scratch';          c = 'E99695'; d = 'No prior art - new work' },
  @{ n = 'infra';                 c = 'CFD3D7'; d = 'Build, CI, tooling, packaging' },
  @{ n = 'measurement';           c = '006B75'; d = 'Instrumentation and budget verification' },
  @{ n = 'checkpoint';            c = 'B60205'; d = 'Phase gate - must pass before the next phase starts' },
  @{ n = 'accessibility';         c = 'F143AB'; d = 'Barrier affecting people with disabilities' }
)

foreach ($l in $labels) {
  gh label create $l.n --color $l.c --description $l.d --force | Out-Null
  Write-Host "label: $($l.n)"
}

Write-Host 'Done. Now run issues-p0.ps1 / issues-p1.ps1 / issues-outline.ps1.'

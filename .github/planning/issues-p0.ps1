# Regenerates the GitHub issues for this phase. Already run once; the issues
# exist. Kept as the template for breaking down Phases 2-7 at their gates.
#
# NOT idempotent: running it again creates duplicates. Close or delete the
# existing issues first, or copy this file and edit the $tasks list.

$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path "$PSScriptRoot\..\..")
$tasks = @()

$tasks += @{
  title = 'P0-01 Repo scaffolding and MSBuild project with hardened flags'
  labels = 'phase-0,infra'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
Create `dhepz.sln` and `src/dhepz.vcxproj` so that an empty `main.cpp` compiles and links with the final flag set. Flags are decided now, not later, because turning `/WX` or static CRT on after thousands of lines exist is a multi-day cleanup.

**Acceptance criteria**
- [ ] Toolset `v143`, `/std:c++20`, `/W4 /WX`, Unicode character set, `/permissive-`
- [ ] Static CRT (`/MT`, `/MTd`) so the installed EXE has no VC++ redistributable dependency
- [ ] Release adds `/Gy /OPT:ICF /OPT:REF`, LTCG, `/GL`
- [ ] Hardening: `/CETCOMPAT`, `/HIGHENTROPYVA`, `/DYNAMICBASE`, `/NXCOMPAT`, `/guard:cf`
- [ ] `src/dhepz.vcxproj` uses **wildcard globs** over `src/**` including `src/modules/**`, so adding a module folder needs no project edit (G5)
- [ ] `app.manifest` embedded: `PerMonitorV2` DPI awareness, `asInvoker` (no UAC), long path aware, Windows 10/11 compatibility GUIDs
- [ ] `app.rc` present and wired, with `version.props` single-sourced into both `cl` and `rc` (see the old `Terminal.vcxproj:54,72-74`)
- [ ] `.gitattributes` enforces **LF only** — `app.rc` embeds JSON verbatim and CRLF corrupts it
- [ ] `x64` only; no Win32/ARM configurations left in the solution

**Verification**
- [ ] `tools\Build.ps1 -Configuration Debug` and `-Configuration Release` both succeed
- [ ] `dumpbin /headers` on the Release EXE confirms CET, high-entropy VA and CFG are set
- [ ] Deliberately introducing a `W4` warning fails the build

**Why (goals)** G2 — static CRT and LTCG are a large part of why cold start can hit 400 ms: no runtime to locate, no redistributable to bootstrap. G5 — the wildcard glob is the mechanism that makes "drop a folder" work.

**Scope** S (3-5 files, no logic)
'@
}

$tasks += @{
  title = 'P0-02 Pin the toolchain and verify it before every build'
  labels = 'phase-0,infra,ported'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
Port the old build''s pinning setup verbatim. An unpinned toolchain means a CI runner image update silently changes the binary, and then a performance regression cannot be attributed.

**Acceptance criteria**
- [ ] `global.json` pins .NET SDK `9.0.304` with `rollForward: disable`
- [ ] `.config/dotnet-tools.json` pins `vpk` at `1.2.0`
- [ ] `version.props` is the single source of version truth
- [ ] `dependencies.lock.json` records Velopack with a **sha256** that is verified, not just a version string
- [ ] `tools\Restore-Dependencies.ps1` restores and fails loudly on a hash mismatch
- [ ] `tools\Test-Toolchain.ps1` asserts VS 17.14.x, VC tools 14.44.35207, Windows SDK 10.0.26100.0 and fails with an actionable message naming the missing component

**Verification**
- [ ] `tools\Test-Toolchain.ps1` passes locally and on the CI runner
- [ ] Editing a hash in `dependencies.lock.json` makes `Restore-Dependencies.ps1` fail

**Why (goals)** Every measured number in this project is meaningless if the toolchain drifts underneath it.

**Scope** S
'@
}

$tasks += @{
  title = 'P0-03 Build and Test entry-point scripts'
  labels = 'phase-0,infra'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
One command to build, one to test, used identically by a human and by CI. If CI runs something different from what a developer runs, CI failures become unreproducible.

**Acceptance criteria**
- [ ] `tools\Build.ps1 -Configuration <Debug|Release>` calls `Test-Toolchain.ps1` first, then MSBuild
- [ ] `tools\Test.ps1` builds if needed, runs the test binary, and returns a non-zero exit code on any failure
- [ ] `tools\Test.ps1 -Filter <pattern>` runs a subset, so a targeted test is cheap during development
- [ ] Both scripts work from any working directory (resolve paths relative to the script, not the caller)
- [ ] Neither script writes anything outside `build/` and `artifacts/`

**Verification**
- [ ] Both exit 0 on a clean tree and non-zero when a test is made to fail deliberately

**Scope** S
'@
}

$tasks += @{
  title = 'P0-04 core/status.h - Status and ErrorCode'
  labels = 'phase-0,ported'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
The error type every layer returns. Ported from the old build. This lands first because `Handle()` in the module contract returns it, so its shape constrains the contract in Phase 3.

**Acceptance criteria**
- [ ] `ErrorCode` enum covering at minimum: `Ok`, `InvalidArgument`, `NotFound`, `IoError`, `ParseError`, `PermissionDenied`, `Unsupported`, `Internal`
- [ ] `Status` carries a code plus a human-readable message; success is cheap to construct and requires no allocation
- [ ] Explicitly convertible to `bool`, and `[[nodiscard]]` so a returned `Status` cannot be silently dropped
- [ ] A helper for attaching file+line context, because config diagnostics require it (G3)
- [ ] Header-only or minimal TU; no Win32 include anywhere in `core/`

**Verification**
- [ ] Unit tests: success/failure construction, message propagation, `nodiscard` enforced (a discarded `Status` fails the `/WX` build)

**Scope** XS
'@
}

$tasks += @{
  title = 'P0-05 Test runner with JSON and JUnit output'
  labels = 'phase-0,infra,ported'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
The project''s own lightweight C++ runner, ported in pattern from the old build. No GoogleTest, no external dependency — Velopack stays the only third-party runtime dependency, and tests must not add one.

**Acceptance criteria**
- [ ] Self-registering test cases via a macro; no central list of tests to edit (same principle as module registration)
- [ ] Assertion macros that report file, line, expected and actual
- [ ] `--filter <pattern>` selects a subset
- [ ] Emits **JUnit XML** so GitHub Actions can annotate failures inline, and JSON for tooling
- [ ] A crashing test is reported as a failure rather than taking the whole run down silently
- [ ] Exit code is non-zero if any test fails

**Verification**
- [ ] A deliberately failing test produces a JUnit entry that GitHub renders as an annotation
- [ ] `--filter` runs only the matching tests

**Why (goals)** Every phase gate in this plan is stated as a measurable assertion. Without a runner those gates are opinions.

**Scope** M
'@
}

$tasks += @{
  title = 'P0-06 core/json - parser with a depth limit'
  labels = 'phase-0,ported,goal:G3-json-ui'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
Ported parser, with the ordered-member behaviour the UI config depends on. This is load-bearing for G3: every screen is JSON, so a parser weakness becomes a UI bug.

**Acceptance criteria**
- [ ] Preserves **member order** — the UI resolver depends on declaration order for layout
- [ ] Hard **depth limit** that returns a `ParseError` instead of overflowing the stack on adversarial or accidentally-nested input
- [ ] Parse errors report line and column, and the message names what was expected
- [ ] Fallback typed access (`GetString(key, default)`) so a missing optional field is not an error at every call site
- [ ] UTF-8 in, UTF-16 out where the API needs `wchar_t`; a BOM is tolerated and stripped
- [ ] Rejects trailing commas and comments — the format must round-trip through `ConvertFrom-Json` in the tooling

**Verification**
- [ ] Unit tests for order preservation, depth-limit rejection, error line/column accuracy, BOM handling, duplicate keys
- [ ] A 10,000-deep nested input returns an error and does not crash

**Scope** M
'@
}

$tasks += @{
  title = 'P0-07 platform/files - three write modes'
  labels = 'phase-0,ported'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
Ported from `logic/platform/files.cpp:84-228`. Three distinct modes exist because they have genuinely different failure semantics, and collapsing them caused a real bug in the old build.

**Acceptance criteria**
- [ ] `WriteTextAtomic` — temp file plus rename, so a crash mid-write cannot corrupt `settings.json`
- [ ] `WriteTextNew` — fails if the target exists, for cases where clobbering is a bug
- [ ] `WriteTextInPlace` — **must call `SetEndOfFile`**. The recorded bug in the old build was shorter JSON leaving stale trailing bytes, producing a file that parsed as garbage
- [ ] All writes are UTF-8 **without BOM** and LF
- [ ] Read helpers tolerate and strip a BOM on input
- [ ] Every function returns `Status`; no exceptions, no silent failure

**Verification**
- [ ] Unit test: write long content, then shorter content in place, and assert no trailing bytes remain
- [ ] Unit test: atomic write with the temp file left behind by a simulated failure leaves the original intact

**Scope** S
'@
}

$tasks += @{
  title = 'P0-08 platform/paths - normalisation and lazy state directory'
  labels = 'phase-0,ported'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
Path handling and the installed-app directory split. This is a security control as much as a convenience: user-supplied paths reach `CreateProcess`.

**Acceptance criteria**
- [ ] `paths::Normalize` resolves `.`, `..`, mixed separators, and trailing separators
- [ ] Long-path aware (`\\?\` prefix where needed), matching the manifest setting
- [ ] `paths::StateDir()` returns `%LOCALAPPDATA%\dhepz\state\` and creates it **lazily** — a tray-only startup that never opens a window must not touch the disk (G1)
- [ ] Program-files paths are treated as read-only; nothing writes next to the EXE
- [ ] Validation helper that rejects paths escaping an expected root, for anything derived from config or user input

**Verification**
- [ ] Unit tests for normalisation cases including UNC paths and `..` escaping
- [ ] Trace assertion: `--tray` startup performs zero directory creations

**Why (goals)** G1 — no disk work on the tray path. Security — path validation is the guard before process launch.

**Scope** S
'@
}

$tasks += @{
  title = 'P0-09 platform/strings - QuoteArg with real CommandLineToArgvW rules'
  labels = 'phase-0,ported'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
Ported from `logic/platform/strings.cpp:70-120`. Windows command-line quoting is genuinely non-obvious — backslashes before a quote double, and a naive implementation is a command-injection vector once folder names come from config or user input.

**Acceptance criteria**
- [ ] Implements the actual `CommandLineToArgvW` rules, including the backslash-doubling case before a closing quote
- [ ] Round-trips: `CommandLineToArgvW(QuoteArg(s))` returns exactly `s`, for every test input
- [ ] Handles embedded quotes, trailing backslashes, spaces, tabs, and the empty string
- [ ] Used by **every** call site that builds a command line; no ad-hoc string concatenation anywhere

**Verification**
- [ ] Round-trip unit tests over a hostile input table: `C:\path with space\`, `a"b`, `a\\`, `""`, `a\"b\\`
- [ ] Autostart registry value and terminal launch both go through it

**Why (goals)** Security. The relevant threat model here is process launching and path handling, not web risks.

**Scope** XS
'@
}

$tasks += @{
  title = 'P0-10 ETW trace with named milestones and ResourceSnapshot'
  labels = 'phase-0,measurement,ported,goal:G2-responsive'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
Ported from `src/instrumentation/performance_trace.cpp:9-183`. This is the measurement backbone — without it, every budget in Part 1 of the plan is unfalsifiable and G1/G2 become vibes.

**Acceptance criteria**
- [ ] TraceLogging provider, **zero cost when no session is listening** — no logging thread, no file IO, no allocation on the hot path (G1: it must not add an idle wakeup)
- [ ] Named startup milestones from process entry through to first frame
- [ ] First-frame milestone is timestamped **after `DwmFlush()`**, so "visible" means actually composited rather than merely submitted
- [ ] Correlation-ID pairs so an input event can be matched to the present that resulted from it
- [ ] `ResourceSnapshot` capturing private bytes, GDI and USER counts via `GetGuiResources`, HWND count, and every cache size
- [ ] A `Measure-Performance.ps1` consumer that starts a session, launches the app, and reports p50/p95

**Verification**
- [ ] With no listener attached, idle CPU is 0.0% and no extra thread exists
- [ ] `Measure-Performance.ps1` produces cold-start and warm-show numbers from an installed Release build
- [ ] Snapshot GDI counts match a known-good manual `GetGuiResources` reading

**Why (goals)** G2 is the top-ranked goal and this is the only thing that can prove or disprove it.

**Scope** M
'@
}

$tasks += @{
  title = 'P0-11 Empty tray-resident process'
  labels = 'phase-0,goal:G1-lightweight,from-scratch'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
`main.cpp`, the infrastructure window, and a tray icon — no UI, no config, no modules. This is the thing whose idle cost gets measured, so it must be genuinely minimal, and it must get two easily-missed details right.

**Acceptance criteria**
- [ ] The infrastructure window is a **real top-level `WS_POPUP`** with `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`, **not** `HWND_MESSAGE`. `HWND_MESSAGE` windows never receive the `TaskbarCreated` broadcast, so the tray icon is lost forever after an Explorer restart (old build: `application_infrastructure_window.cpp:34-49,97-100`)
- [ ] `Shell_NotifyIconW(NIM_SETVERSION)` is called **after** the add, which is what enables modern `NIN_*` callback semantics (old build: `application_container.cpp:834-856`)
- [ ] `TaskbarCreated` is registered and re-adds the icon
- [ ] Message loop blocks in `GetMessageW`. No polling, no timers, no idle processing
- [ ] Exactly **one thread** alive at idle
- [ ] Tray menu with Exit only, for now
- [ ] Startup milestones emitted via the P0-10 trace

**Verification**
- [ ] Idle CPU 0.0% averaged over 60 s
- [ ] Kill and restart `explorer.exe`; the tray icon comes back
- [ ] Thread count at idle is 1; no `WM_TIMER` armed
- [ ] 60 min soak: private bytes flat within tolerance

**Why (goals)** G1. Also the baseline subject: this process''s idle RSS is the floor everything else is measured against.

**Scope** M
'@
}

$tasks += @{
  title = 'P0-12 CI green on the first commit'
  labels = 'phase-0,infra,ported'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
Port `ci.yml` from the old build. CI lands in Phase 0 rather than later, because retrofitting a green-main rule onto an already-red history means a cleanup phase nobody wants to do.

**Acceptance criteria**
- [ ] `windows-2022` runner, `actions/checkout@v7`, `actions/setup-dotnet@v6` pinned to `9.0.304`, `microsoft/setup-msbuild@v3`
- [ ] Steps in order: `Restore-Dependencies.ps1`, `Test-Toolchain.ps1`, Debug build + test, Release build + test
- [ ] JUnit results uploaded and surfaced as inline annotations
- [ ] Triggers on push to `main` and on every pull request
- [ ] A warning fails the build (`/WX` is already on — confirm CI does not relax it)
- [ ] `release.yml` ported too, but its version-bump and publish path is not exercised until Phase 6

**Verification**
- [ ] The first pushed commit is green
- [ ] A deliberately broken commit on a branch fails CI and blocks the PR

**Open question carried from the plan:** the old `release.yml` has Indonesian error strings. Decide whether to keep them or use the English normalisation.

**Why (goals)** "Every commit on main is CI-green" is a hard rule in `AGENTS.md`. This issue is what makes it enforceable rather than aspirational.

**Scope** S
'@
}

$tasks += @{
  title = 'P0-13 Record the measured baseline from an installed Release binary'
  labels = 'phase-0,measurement,checkpoint'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
Replace the estimated numbers in Part 1 of the plan with measured ones. Every target in the plan today is my estimate; this issue is where estimates become facts.

**Acceptance criteria**
- [ ] Numbers come from an **installed** Release build, never the build tree — different paging behaviour and asset layout mean build-tree numbers do not predict the shipped app
- [ ] Recorded: idle private bytes (tray, no window), cold start to visible window p50/p95 over at least 20 runs, thread count, GDI/USER handle counts
- [ ] Machine, Windows build, and toolchain versions recorded alongside, so a later comparison is valid
- [ ] Part 1 of `.docs\plan.md` updated: if the floor lands well under a bound, the bound is **not** lowered to create artificial pressure; if it lands over, that is investigated as a structural problem rather than becoming a licence to raise the bound
- [ ] Fixed budgets (0.0% idle CPU, zero drift, flat handles, one thread) are **not** re-baselined — they measure change, not size

**Verification**
- [ ] `Measure-Performance.ps1` reruns and reproduces the recorded numbers within noise
- [ ] The recorded baseline is committed somewhere durable (issue comment is acceptable; `.docs/` is gitignored)

**Scope** S, but blocking — this is the Phase 0 gate.
'@
}

$tasks += @{
  title = 'CHECKPOINT Phase 0 gate'
  labels = 'phase-0,checkpoint'
  milestone = 'Phase 0 - Skeleton and measurement floor'
  body = @'
Phase 1 does not start until every box here is ticked. The point of a gate is that it is cheaper to find a structural problem now than after 5,000 lines depend on it.

- [ ] An empty tray-resident process runs, with a tray icon that survives an Explorer restart
- [ ] Idle CPU 0.0% over 60 s, exactly one thread, no timer armed
- [ ] Debug and Release both build clean with `/W4 /WX`
- [ ] CI is green, and it ran the same scripts a developer runs
- [ ] Toolchain pin verified before every build
- [ ] Test runner works, emits JUnit, and a failing test fails CI
- [ ] Unit tests exist and pass for `Status`, `json` (including the depth limit), the three file write modes, `paths::Normalize`, and `QuoteArg` round-tripping
- [ ] ETW trace produces named startup milestones and a `ResourceSnapshot`, with zero cost when nothing is listening
- [ ] Measured baseline recorded from an **installed** Release binary, and Part 1 of the plan updated to match
- [ ] Human review of the recorded numbers before Phase 1 starts
'@
}

foreach ($t in $tasks) {
  $f = New-TemporaryFile
  [System.IO.File]::WriteAllText($f.FullName, ($t.body -replace "`r`n", "`n"))
  $url = gh issue create --title $t.title --body-file $f.FullName --label $t.labels --milestone $t.milestone
  Write-Host "$url  $($t.title)"
  Remove-Item $f.FullName -Force
}

# Regenerates the GitHub issues for this phase. Already run once; the issues
# exist. Kept as the template for breaking down Phases 2-7 at their gates.
#
# NOT idempotent: running it again creates duplicates. Close or delete the
# existing issues first, or copy this file and edit the $tasks list.

$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path "$PSScriptRoot\..\..")
$tasks = @()

$tasks += @{
  title = 'PHASE 2 outline - Config-driven UI'
  labels = 'goal:G3-json-ui'
  milestone = 'Phase 2 - Config-driven UI'
  body = @'
Outline only. Broken into detailed issues at the Phase 1 gate, once the render backend and worker pool exist and the real frame-time numbers are known.

**Scope**
- Port the 14 shared components, each carrying `automation` metadata from the start so accessibility in Phase 7 is not a retrofit
- `assets/ui/core.json` — window frame, styles, themes
- Config resolver producing an immutable `ResolvedUiDocument`
- Theme dark/light, with the debounced adapter and the **deferred WinRT subscription** (subscribing eagerly costs startup time for a signal that may never arrive)
- Focus coordinator
- List virtualisation
- Build-time merge step; `New-UiScreen.ps1` adapted from the old build (validates the route ID against a slug regex, refuses overwrite, round-trips its own output through `ConvertFrom-Json`, writes UTF-8 **without BOM**)
- **Two-tier config resolution with corrupt-override quarantine** (old build: `ui_config_gate.cpp:71-163`) — a bad override at bootstrap falls back to embedded and keeps running; a bad override on hot reload keeps the *live* document. This is the concrete mechanism behind G5
- Full validation with file+line diagnostics

**Gate**
- A screen defined purely in JSON renders and is interactive, with no module system yet
- A deliberately malformed JSON fails the **build** with a precise file+line message
- A corrupt user override falls back to embedded and the app still runs
- Route switch ≤ 50 ms warm

**Do not start** until the Phase 1 gate (#23) passes.
'@
}

$tasks += @{
  title = 'PHASE 3 outline - The contract and the gate'
  labels = 'goal:G4-contract,goal:G5-plug-and-play'
  milestone = 'Phase 3 - The contract and the gate'
  body = @'
Outline only. Broken into detailed issues at the Phase 2 gate.

**This is the key phase.** Everything the project is for depends on getting this contract right, and Hyrum''s Law applies hard: once a module ships against it, changing it breaks every module.

**Scope**
- `ModuleHost` — parent hands down `Surface / Settings / Storage / Process / Status / Log / RequestRoute / Peers`
- `ModuleDescriptor` — child hands up `moduleId / tabLabel / order / showInTabs / settingsRoute? / DeclaredActions / DeclaredBindings / DeclaredCapabilities / Bind / Handle / Release`
- `ModuleRegistry` self-registration. **Modules compile directly into the EXE, never into a static `.lib`** — the linker discards unreferenced objects and silently drops static self-registration
- `AppGate`: `ResolveConfig / CollectModules / PairAndValidate / MountAccepted / Activate / Dispatch / ApplyConfig / Peers / Diagnostics`
- `module.json` manifests; declared capabilities must match or the gate refuses the module
- Capabilities: `settings:all` (settings only, single claimant) and `config:write` (ui-editor only)
- Three-stage scan trigger, split by cost
- Diagnostics module — built in, never excluded
- `New-Module.ps1`, modelled on `New-UiScreen.ps1`
- Degraded mode: a broken module warns and is skipped; the app keeps running

**Gate**
- Three module folders: one healthy, one with a typo''d action, one with malformed JSON. App starts, healthy one works, both broken ones appear in diagnostics **with reasons**, nothing else is affected
- Deleting the healthy folder and rebuilding removes it cleanly, **with no other file edited**
- CI asserts all of the above, so a regression in degraded mode fails the build

**Do not start** until the Phase 2 gate passes.
'@
}

$tasks += @{
  title = 'PHASE 4 outline - Terminal module'
  labels = 'goal:G4-contract'
  milestone = 'Phase 4 - Terminal module'
  body = @'
Outline only. Broken into detailed issues at the Phase 3 gate.

Build **`terminal` and nothing else**, as the reference module. This is where the contract earns its keep or gets fixed — refining it against one real module is far cheaper than discovering its gaps across four.

**Scope**
- Launch PowerShell, PowerShell as Admin, and WSL
- Recent folders; venv toggle
- **WSL distros enumerated**, not hardcoded: `wsl -l -q` via `RunCapture` on a worker, cached for the session with an explicit refresh, so installing a distro later needs no code change
- The `wsl.exe` output decoder: `wsl.exe` emits its **own** messages as UTF-16LE while passing the inner command''s UTF-8 through unchanged. Sniff a BOM *or* ≥75% NULs in odd byte positions (old build: `process.cpp:19-42`)
- Worker offload for distro enumeration and folder validation
- `New-Module.ps1` finalised from what this module actually needed

**Gate**
- Launches work for every enumerated distro; the screen feels smooth
- UI thread never blocks — verified from the trace, not by feel
- Installing a new distro makes it appear **with no rebuild**
- The module touches nothing outside its own folder
- The contract is not changed again after this without a stated reason

**Do not start** until the Phase 3 gate passes.
'@
}

$tasks += @{
  title = 'PHASE 5 outline - Settings and UI editor'
  labels = 'goal:G5-plug-and-play,goal:G3-json-ui'
  milestone = 'Phase 5 - Settings and UI editor'
  body = @'
Outline only. Broken into detailed issues at the Phase 4 gate.

This phase is the **proof of G5**: two modules added by copying `terminal`''s folder shape, with no central file edited.

**Scope**
- `settings` as the second module: global settings (theme, confirm-before-run, autostart) in one shared `settings.json` under `settings:all`, plus a peer list built from `ModuleHost::Peers()` that opens each module''s own settings screen via `RequestRoute` — which is what stops "open a specific screen''s settings" from becoming sibling coupling
- **Autostart reconciliation lives in the gate, not the module.** The module writes `global.autostart`; the gate writes or deletes the `HKCU\...\Run` value (`"<installed exe>" --tray`, quoted via `QuoteArg`). No registry reach in `ModuleHost`, no new capability
- Reconcile at **every launch** as well as on toggle — an update moves the install path and leaves a stale entry pointing at a deleted EXE
- The toggle **reads the registry**, not `settings.json`, so disabling it via Task Manager''s Startup tab is reflected honestly. A failed write surfaces as a `Status` error and leaves the toggle off
- `ui-editor` as the third module, with `config:write` and the validate → swap → rebuild → Save/Discard hot-apply path
- Unknown `modules.*` sections are preserved, not pruned, so removing and restoring a module folder does not lose its settings

**Gate**
- Both modules added **without editing any file outside their own folders**
- Global settings persist and are visible to `terminal` as read-only
- `terminal`''s settings screen opens from the peer list with no sibling reference anywhere
- Autostart on → `Run` value exists and points at the installed EXE with `--tray`; a **real reboot** starts to tray with no window flash. Off → value gone. Removed externally → toggle reads off
- Hot-apply: a valid edit updates the live UI without restart; an invalid edit is refused with file+line and leaves the live UI untouched; Discard restores exactly; a saved-then-broken override falls back to embedded on next start with a warning

**Do not start** until the Phase 4 gate passes.
'@
}

$tasks += @{
  title = 'PHASE 6 outline - Resident hardening and shippable installer'
  labels = 'goal:G1-lightweight,infra'
  milestone = 'Phase 6 - Resident hardening and shippable installer'
  body = @'
Outline only. Broken into detailed issues at the Phase 5 gate.

**Scope**
- Named-pipe single instance, with the pipe server started **before any window**. On the top-level `WS_POPUP` infrastructure window, **not** `HWND_MESSAGE` — `HWND_MESSAGE` windows never receive `TaskbarCreated`, so the tray icon is lost forever after an Explorer restart
- Request-ID dedup and a bounded IPC queue
- Tray menu with `Shell_NotifyIconW(NIM_SETVERSION)` after the add, which is what enables modern `NIN_*` semantics
- Jump list; **AppUserModelID** so the pinned taskbar icon groups with the running window and the jump list attaches
- Velopack 1.2.0 packaging and updater, ported from the working setup. The update check runs **only** on an explicit `--update` invocation, never on the launch path
- `release.yml` exercised for real: manual dispatch, requires `main`, requires a green CI run for the exact SHA, version bump committed as `github-actions[bot]`, `dotnet vpk download github` for deltas, `vpk upload github --publish`, installer SHA-256 in the step summary
- Install/uninstall hooks; `%LOCALAPPDATA%\dhepz\state\` separation from the read-only install directory
- Release-retained-resources-on-hide; **native-peer suspend/resume with rollback** — snapshot draft text, `EM_GETSEL`, `EM_GETFIRSTVISIBLELINE`, focus; complete IME composition **first**; caret survives destroy/recreate
- `SetUnhandledExceptionFilter` writing a **minidump** to `%LOCALAPPDATA%\dhepz\state\crash\`. The old build has no crash handling at all, so a death at hour six currently leaves nothing to diagnose
- 8-hour soak, leak audit, DPI and multi-monitor pass

**Gate**
- Rapid-launch race test never produces a second window — the old "Terminal (2)" bug cannot reproduce
- Tray icon survives an Explorer restart
- Install → pin to taskbar → click → cold start within budget; click again while resident → warm show within budget
- Update over an existing install preserves `settings.json` and any UI override, and leaves autostart pointing at the **new** install path
- Uninstall terminates the resident instance, removes the tray icon, and deletes the `Run` value
- 8 h soak within the drift budget; hide/restore 100× flat with no lost caret position
- Update check does not run on the launch path

**Open, to decide here:** code signing (unsigned installers trigger SmartScreen on download — irrelevant for personal use, needed if shared) and whether to add a watchdog on top of the minidump filter.

**Do not start** until the Phase 5 gate passes.
'@
}

$tasks += @{
  title = 'PHASE 7 outline - Remaining modules and accessibility'
  labels = 'goal:G5-plug-and-play,accessibility'
  milestone = 'Phase 7 - Remaining modules and accessibility'
  body = @'
Outline only. Broken into detailed issues at the Phase 6 gate.

**Scope**
- Chrome profiles, Claude inject, JSON editor — each a drop-in module, each ported from the old build
- Accessibility (UIAutomation), deferred to here deliberately: the plan says wait until a single screen is genuinely smooth. Components already carry `automation` metadata from Phase 2, so this is wiring rather than a retrofit
- Global hotkey **if** it is wanted by then: `RegisterHotKey` on the infrastructure window, a settings field for the combination, conflict handling. No timer and no thread, so it does not threaten G1

**Gate**
- Every module added **without editing any file outside its own folder** — three more independent confirmations of G5
- Screens are navigable by keyboard and exposed to a screen reader

**Do not start** until the Phase 6 gate passes.
'@
}

foreach ($t in $tasks) {
  $f = New-TemporaryFile
  [System.IO.File]::WriteAllText($f.FullName, ($t.body -replace "`r`n", "`n"))
  $url = gh issue create --title $t.title --body-file $f.FullName --label $t.labels --milestone $t.milestone
  Write-Host "$url  $($t.title)"
  Remove-Item $f.FullName -Force
}

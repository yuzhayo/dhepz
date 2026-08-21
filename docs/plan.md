# dhepz — canonical plan through Phase 4

This is the single current product and architecture checklist. Old phase issue
bodies, integration-checkpoint bodies, handoffs, and audits are historical Git
data, not requirements.

## Product

dhepz is a simple Windows launcher. Its core is a generic engine. A thin
orchestrator connects parent-owned UI and services to a child feature for each
screen. A feature owns its screen JSON, state, and business logic in one folder.

The Phase 4 terminal behavior is based on the working reference at
`C:\VSCODE\New folder\Open-terminal`:

- editable path with recent-history suggestions;
- native Explorer folder browse;
- one venv toggle that checks, creates when absent, and activates the venv;
- direct Windows Terminal actions for PowerShell Admin, PowerShell, and WSL;
- no Cmd action, Admin toggle, separate Validate button, or app confirmation.

Windows Terminal is the host (`wt.exe`). PowerShell and WSL are the selected
shell/profile. Admin is a direct action, not persistent screen state. Windows
may show its own UAC prompt for the elevated action; dhepz adds no confirmation.

## Architecture

```text
wWinMain
  -> production composition root
  -> core UI engine (window, JSON, layout, renderer, reusable components)
  -> orchestrator (discover, pair, activate, dispatch, lifecycle)
  -> child feature (screen JSON + state + business logic)
  -> narrow parent contract (process, folder picker/probe, settings, status)
  -> state patch back to the screen
```

Rules:

- Core and parent services are generic; they never contain terminal behavior.
- The orchestrator only connects layers. It does not render, perform I/O, or
  implement feature decisions.
- A child imports only its own files plus the parent contract. It never imports
  a sibling, gate implementation, presenter, window, or platform implementation.
- The parent knows a child only as `ModuleDescriptor`; the child knows the
  parent only as `ModuleHost`.
- One feature folder contains its `module.json`, `screen.json`, coordinator,
  state, and business logic.
- Adding or removing a feature folder must not require a feature-specific edit
  to `main`, core, UI parent, or orchestrator.
- Screen composition belongs in JSON. Reusable rendering behavior belongs in
  the core UI engine. Business behavior belongs in the child.
- Do not add a feature, control, setting, confirmation, shell, or workflow that
  the user did not request. Stop and ask before resolving product ambiguity.
- A green test is not evidence of product alignment. Compare behavior with this
  document and the named reference before verification.

## Cleanup checklist through Phase 4

No Phase 5 work starts until every unchecked item below is reviewed against the
running product. This correction cycle does not add or run automated tests.

Checked items below mean the code boundary or implementation is present in the
current checkout. They do not replace the manual Phase 4 clean gate.

### Phase 0 — generic core boundary

- [x] Inspect `main`, core, platform, worker, JSON, and resource code; remove or
      relocate any terminal-specific decision outside the terminal child.
- [x] Keep `main` limited to argument handling and production composition.
- [x] Keep process execution, settings persistence, and folder interaction as
      generic parent services with no shell-specific policy.
- [x] Confirm the resident path creates no permanent polling loop or feature
      worker.

### Phase 1 — parent window and UI engine

- [x] Keep `AppWindow`, rendering, layout, focus, and reusable components
      feature-neutral.
- [x] Define compact initial window size and placement suitable for Quick
      Actions without hard-coding terminal controls in C++.
- [x] Preserve JSON-controlled component layout and visible interaction states.
- [x] Keep close/hide/reopen lifecycle independent from child business logic.

### Phase 2 — JSON screen engine

- [x] Ensure JSON can express the terminal layout: path input, history surface,
      browse button, venv toggle, three action buttons, WSL selection, and status.
- [x] Put terminal labels, order, spacing, bindings, and actions only in the
      terminal screen JSON.
- [x] Keep the presenter generic: resolve bindings, emit semantic actions, and
      apply state patches without naming terminal.
- [x] Remove renderer or schema additions that exist only to support the wrong
      Cmd/Admin-toggle/Validate design and have no reusable purpose.

### Phase 3 — thin orchestrator and parent-child contract

- [x] Review `AppGate` and its services by responsibility; retain only discovery,
      pairing, validation, activation, dispatch, lifecycle, and capability grants.
- [x] Keep blocking work in generic parent services and completion delivery on
      the UI thread; keep feature policy in the child.
- [x] Add a narrow native folder-picker request/result seam for child screens;
      do not put Explorer-specific behavior in terminal logic.
- [x] Confirm terminal reaches process, folder, settings, status, and state-patch
      operations only through `ModuleHost`.
- [x] Confirm the parent reaches terminal only through `ModuleDescriptor` and
      registry metadata.
- [x] Confirm no sibling imports, globals, central feature switch, or terminal
      branch exists in core/orchestrator code.
- [x] Keep the file tree readable; split orchestration from service
      implementations instead of growing one gate file.

### Phase 4 — terminal child complete

- [x] Replace the current flat screen with the approved Quick Actions layout.
- [x] Make the path editable and keep recent paths attached to that input.
- [x] Wire the browse icon to the parent native Explorer folder picker and put
      the selected path back into state.
- [x] Remove Cmd from JSON, manifest bindings, state, logic, and launch builders.
- [x] Remove `admin` state and the Admin toggle.
- [x] Remove the separate Validate button; validate as part of browse, path
      acceptance, venv preparation, or launch.
- [x] Build the normal PowerShell action as `wt.exe` with the PowerShell profile
      and selected directory.
- [x] Build PowerShell Admin as the same Windows Terminal action through the
      parent's elevated process operation.
- [x] Open every enumerated WSL distro inside Windows Terminal and map the
      selected Windows/UNC path correctly.
- [x] Implement venv OFF as direct launch.
- [x] Implement venv ON as check -> create when absent -> activate -> launch,
      with Windows and WSL activation rules owned by terminal.
- [x] Persist only useful terminal state: recent paths and the venv preference;
      do not persist Admin as a mode.
- [x] Keep WSL enumeration/cache/refresh inside the terminal child while all
      process execution crosses the parent contract.
- [x] Keep cancellation and operational errors visible without disabling the
      healthy terminal feature.
- [x] Review the resulting file tree and remove obsolete Cmd/Admin-toggle/
      Validate helpers and bindings.

### Phase 4 clean gate

- [ ] User reviews the file tree and confirms core, orchestrator, parent, and
      child ownership are understandable.
- [ ] User reviews the visible Quick Actions screen before automated validation.
- [ ] From a chosen path, each approved direct action opens Windows Terminal in
      the expected location.
- [ ] Venv OFF and ON behave as specified, including creation when absent.
- [ ] Browse Explorer, path history, WSL selection, and reopen behavior work in
      the production executable.
- [ ] No Phase 5 code, issue breakdown, test expansion, performance audit, ETW,
      packaging, or release work is mixed into this correction.

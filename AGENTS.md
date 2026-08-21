# dhepz — agent operating rules

Windows Win32 C++20 tray-resident launcher. MSBuild (`v143`, `/std:c++20 /W4 /WX`, static CRT, Unicode).
The single tracked product plan is `docs/plan.md`. Read it before proposing or
implementing architecture, UI, or feature behavior.

## Non-negotiable goals

| ID | Goal |
|---|---|
| G1 | Lightweight resident: idle RSS stays within its bound and does not drift; 0.0% idle CPU; no timers/threads/wakeups when idle; flat handles/RSS over an 8 h soak. Idle RSS is a bound to stay under, **not** a number to chase. |
| G2 | Responsive and good-looking: fast to appear from a pinned-taskbar click, warm *or* cold; the UI thread never blocks on IO or process work; visual quality is part of the goal. **When G1 and G2 conflict, G2 wins.** |
| G3 | UI driven by JSON config: adding a screen is one JSON file |
| G4 | Modular with an explicit parent/child contract (`ModuleHost` / `ModuleDescriptor` / `AppGate`); UI + logic paired per module folder |
| G5 | Plug-and-play: drop a module folder, rebuild, it appears; no central file is ever edited; a broken module warns and the app keeps running |

A change that improves anything else at the cost of one of these is a regression. Say so rather than shipping it.

## Skills

Curated skills are in `.github/skills/`, shared checklists in `.github/references/`.
`.github/skills/using-agent-skills/SKILL.md` maps work to the right skill.
These skills assume a web/JS project. Project translations, which win over the skill text:

- **Measurement** — the metrics are cold-start-to-visible-window, warm-show, input-to-paint, then idle RSS and handle count. Not Core Web Vitals, LCP, or bundle size. Ignore Lighthouse, npm, and bundler guidance entirely. Always measure an installed Release build.
- **Tests** — the project's own lightweight C++ runner, run via `tools\Test.ps1`. Not Jest/Playwright/RTL. `references/testing-patterns.md` is illustrative only; take the principles, drop the syntax.
- **Observability** — a local structured trace and diagnostics module. No OpenTelemetry, no network telemetry, no external collector. Nothing may add an idle wakeup (G1).
- **API design** — the surface being designed is the `ModuleHost` / `ModuleDescriptor` contract. Hyrum's Law applies hard: once a module ships against the contract, changing it breaks every module.
- **Frontend UI** — screens are JSON over 14 compiled component types, not React. Design-system talk maps to `core.json` styles and themes. Accessibility is UIAutomation, deferred until screens are smooth.
- **Deprecation, shipping, security web sections** — mostly out of scope. Security relevant here is process launching, path validation, and IPC, not OWASP web risks.

## Hard rules

- **Never edit a central file to add a module.** If a change requires it, the contract is wrong — fix the contract.
- **No module may reference a sibling, a global, or a layer above it.** Cross-module reach exists only as a declared, gate-granted capability.
- **G2 outranks G1.** Never trade responsiveness or visual quality for idle RSS. Zero idle CPU and zero drift remain absolute.
- Ships as a per-user Velopack installer. Program files are read-only at runtime; all user state lives under `%LOCALAPPDATA%\dhepz\state\`. An update replaces the install directory wholesale and must not touch user state.
- Performance numbers come from an **installed Release build**, never the build tree.
- Velopack is the only third-party runtime dependency.
- Modules compile directly into the EXE, never into a static `.lib` — the linker discards unreferenced objects and silently drops static self-registration.
- LF line endings only. `app.rc` embeds JSON verbatim; CRLF corrupts it.
- Every commit on `main` is CI-green. Releases are cut only from a commit CI already passed.
- The working Open Terminal v1 reference is `C:\VSCODE\New folder\Open-terminal`.
  Read it before changing terminal behavior. It is reference-only.
- Never infer product behavior from tests, old issues, audits, or agent-written
  acceptance criteria when they conflict with `docs/plan.md` or the user.
- Do not add shells, controls, confirmations, settings, or workflows without
  explicit user approval.

## Current working order

`docs/plan.md` is authoritative through the Phase 4 correction. Do not start
Phase 5, create replacement issue trees, or expand verification until its
Phase 4 clean gate is reviewed by the user. Work from product behavior first;
tests verify an approved design and never define it.

`main` is protected: do not commit or push without explicit user authorization.

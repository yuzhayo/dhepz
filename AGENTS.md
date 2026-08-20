# dhepz — agent operating rules

Windows Win32 C++20 tray-resident launcher. MSBuild (`v143`, `/std:c++20 /W4 /WX`, static CRT, Unicode).
The plan lives at `.docs/plan.md` (gitignored, single live copy). Read it before proposing architecture.

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

## Where the work is tracked

Tasks live in **GitHub Issues**, not in a markdown checklist. One milestone per phase (Phase 0–7).

- Issues #1–#14 — Phase 0, detailed. #15–#23 — Phase 1, detailed.
- Issues #24–#29 — one outline per remaining phase, broken down at the preceding gate.
- **Issue #30 is the working order. Read it before touching anything, and do the next unchecked item.**
- `CHECKPOINT` issues are phase gates. **No phase starts until the previous gate is fully ticked.**
- `.github/planning/` holds the scripts that generated those issues, and the issue-body template to reuse when a later phase is broken down. See its README.

**One issue at a time, one PR each, in #30's order. Never work two in parallel.** Issue numbers are creation order and carry no meaning — #30's list is the order, and in one place it deliberately runs backwards through the numbers. Parallel work on items that merely look independent produces interleaved history nobody can bisect, and settles a shared interface against three simultaneous guesses instead of one refinement.

`main` is protected: CI must be green and direct pushes are rejected, so every change lands as a PR.

When implementing, work from the issue: its acceptance criteria and verification steps are the definition of done for that task. If reality contradicts an acceptance criterion, say so and amend the issue rather than quietly doing something else.

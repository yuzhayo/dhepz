# Planning scripts

The work breakdown lives in [GitHub Issues](https://github.com/yuzhayo/dhepz/issues),
not in this folder. These scripts are what created those issues, kept in the
repo so any agent or machine can see how the breakdown was generated and follow
the same shape for later phases.

| File | What it did |
|---|---|
| `setup-labels-milestones.ps1` | 8 phase milestones + 13 labels. Idempotent, safe to re-run. |
| `issues-p0.ps1` | Issues #1–#14 — Phase 0 in full detail, #14 is the gate |
| `issues-p1.ps1` | Issues #15–#23 — Phase 1 in full detail, #23 is the gate |
| `issues-outline.ps1` | Issues #24–#30 — one outline per phase 2–7, plus the META index |

## These are not idempotent

`setup-labels-milestones.ps1` is. The three `issues-*.ps1` are not — re-running
one creates a second copy of every issue it contains. They have already been
run against `yuzhayo/dhepz`.

## Why Phases 2–7 are only outlines

Detailing them now would be guessing. Phase 0 ends with a measured baseline
(#13) that replaces every estimated budget number in the plan, and Phase 1 ends
with a decision about whether GDI is fast enough or Direct2D is needed (#23).
Both outcomes change what the later issues should say.

When you reach a phase gate, copy the nearest `issues-*.ps1`, replace the
`$tasks` list, and run it. The structure each issue body follows:

```
<one paragraph: what, and why it is a separate issue>

**Acceptance criteria**
- [ ] ...

**Verification**
- [ ] <how you prove it, not just that it compiles>

**Why (goals)** <which of G1-G5 this serves, and what it trades away>

**Scope** S / M / L
```

The `Verification` section is the part that matters. An acceptance criterion an
agent cannot check is a criterion that gets marked done without being true.

## Requirements

`gh` authenticated against the target repo. Run from anywhere — the scripts
resolve the repo root from `$PSScriptRoot`.

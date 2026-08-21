P4-06 Phase 4 gate verification

Scope
- Consolidate Phase 4 verification steps and run the gate checklist.

Acceptance criteria (gate)
- Terminal module launches PowerShell (and other enumerated shells) and returns a usable prompt.
- UI thread never blocks when launching a shell; measured traces confirm the work is offloaded.
- WSL distro enumeration is cached and refreshable; adding a distro is visible after refresh.
- The terminal module touches only its own folder; adding or deleting the folder appears/disappears without editing other files.
- CI asserts the gate (integration tests + instrumentation).

Verification
- Integration tests and CI-run instrumentation demonstrate warm-start budgets and worker-offload behavior.
- The gate checklist is recorded in the PR and must pass before Phase 5 is broken down.

Scope: M

Do not start until: P4-01..P4-05 are merged and their core tests pass.
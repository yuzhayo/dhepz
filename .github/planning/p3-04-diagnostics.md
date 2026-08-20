P3-04 Diagnostics module and degraded-mode reporting

Scope
- Build an in-process diagnostics module that records and surfaces module load/validation failures.
- Ensure diagnostics are always compiled in and cannot be excluded by build flags.
- Expose diagnostics via an in-app view (for reviewers) and via CI-readable artifacts.

Acceptance criteria
- A malformed module.json is recorded with file+line and a human-friendly reason.
- A module with a typo'd action is recorded with the affected module and action name.
- Diagnostics do not block the host or cause other modules to fail.

Verification
- Integration test: three folders (healthy, typo'd action, malformed JSON); start app and assert diagnostics include both broken folders with reasons and the healthy folder is active.
- CI step emits diagnostics artifact when the fixture set runs.

Why this next
- Degraded-mode visibility is critical to ensure broken modules are visible and diagnosable without stopping the app.

Scope: S

Do not start until: P3-03 is implemented and can notify diagnostics.
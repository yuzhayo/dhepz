P3-06 Broken-module fixtures and contract-validation CI

Scope
- Add three fixture module folders to tests/fixtures/modules:
  1) healthy module
  2) module with a typo'd action
  3) module with malformed module.json
- Add CI step that runs the gate against these fixtures and asserts the expected degraded-mode behaviour.

Acceptance criteria
- CI asserts the healthy module becomes active while the broken modules are reported in diagnostics.
- CI fails if a broken module causes the app to crash or if the healthy module disappears.
- CI publishes a diagnostics artifact describing failures in a machine-readable and human-friendly way.

Verification
- CI run with the fixture set passes the gate checks and produces the diagnostics artifact on failure for debugging.

Why this next
- Repeated enforcement in CI prevents regressions in the gate and degraded mode.

Scope: S

Do not start until: P3-05 is merged.

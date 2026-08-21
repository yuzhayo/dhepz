P4-02 Process launch and RunCapture for shells

Scope
- Implement robust process launch helpers and RunCapture used by the terminal module for launching PowerShell, Admin elevation, and other shells.
- Ensure process output capture is deadlock-free, bounded, deadline-aware, and offloaded to a worker when blocking.
- Add strict command-line building using str::QuoteArg and safe argument passing.

Acceptance criteria
- The RunCapture helper returns exit code, stdout/stderr, and a timed-out status; it never blocks the UI thread.
- Process launch paths use QuoteArg for all child arguments; no ad-hoc concatenation.
- Elevation and Admin launch paths are present and fail with an explanatory Status when unavailable.

Verification
- Tests that RunCapture captures stdout/stderr and times out at the given deadline.
- Smoke test: launching PowerShell and retrieving its version string via RunCapture on a worker.

Scope: M

Do not start until: P4-01 scaffold is merged.
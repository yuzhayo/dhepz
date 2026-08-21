P4-04 Recent folders, venv toggle, and folder validation

Scope
- Implement recent-folder history in the terminal module, user-visible in the terminal UI.
- Add venv toggle support (virtualenv/venv detection and activation toggle) in the module UI.
- Validate folder paths on a worker and surface human-friendly diagnostics when invalid.

Acceptance criteria
- Recent folders persist per-session and across runs in the module's own storage section.
- Venv toggle detects standard virtualenv layouts and toggles activation in the launched shell.
- Folder validation is performed on a worker; invalid folders show a Status diagnostic without blocking the UI.

Verification
- Unit tests for recent-folder add/remove and persistence.
- Integration test for venv detection logic with sample folder layouts.

Scope: S

Do not start until: P4-03 WSL enumeration is in review.
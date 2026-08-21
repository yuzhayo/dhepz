# ADR 0002 — Parent-owned asynchronous settings persistence

Status: accepted (Integration Checkpoint IC-03). Links: `.docs/plan.md` Part 3,
decision 13; `docs/audit.md` H-12.

## Context

Phase 4 initially kept settings in a map owned by each `GateHost`. That made a
unit test look persistent while every process restart lost terminal recent
folders. Moving the file into the future settings module would also violate the
parent-owned lifecycle rule and delay real persistence until Phase 5. Disk I/O
cannot run on the UI thread, and silent tray construction must remain inert.

## Decision

1. Parent `SettingsAccessService` owns one `SettingsStore`; `AppGate` only
   composes and configures that service. Production resolves exactly one path:
   `paths::Join(paths::StateDir(), L"settings.json")`; creation goes through
   `paths::EnsureStateDir()` immediately before the first write. Tests may
   inject a path before gate start and never use the user's state directory.
2. Construction performs no I/O, creates no directory, and starts no thread.
   `StartSettingsLoad` is the first-read trigger. It uses a run-once worker and
   posts a typed completion to the UI thread. A missing or corrupt file produces
   defaults plus a `Status` diagnostic rather than failing gate startup.
3. The physical document is an ordered JSON object with `global` and
   `modules.<moduleId>` sections. Reads and writes change only the addressed
   value. Unknown root members, module sections, keys, and JSON value types are
   retained. A normal `GateHost` can address only its module section and read
   globals; only `SettingsAllFacet` can address all sections or write globals.
4. Each accepted write updates the UI-owned snapshot first and receives a
   revision. One run-once writer drains to the newest accepted serialized
   snapshot in revision order using `files::WriteTextAtomic`. An older write can
   never finish after a newer write. Save failures return a completion `Status`
   and are retained in store diagnostics.
5. Graceful shutdown stops accepting UI work and joins at most the active
   run-once writer, which drains the newest already-accepted snapshot before it
   exits. Completion delivery may be suppressed during teardown, but the file
   cannot be torn or rolled backward. No worker or timer remains at idle.
6. Modules never receive the path and never call file APIs. The Phase 5
   `settings` module is an editor through `settings:all`, not the store owner.

## Consequences

- Settings survive a complete gate/process-owner teardown and preserve data
  written by future modules.
- First paint and tray-only construction do not wait for or touch settings I/O.
- Callers must request readiness and handle defaults explicitly. Persistence is
  asynchronous, so a successful immediate write status means accepted and
  queued; final I/O status arrives through the parent diagnostics/completion
  path.

# ADR 0003 — Module runtime quarantine and window lifetime

Status: accepted. Canonical product plan: `docs/plan.md`.

## Context

A module boundary can fail because user input is invalid, an OS operation is
unavailable, or the user cancelled it. Those are expected action outcomes and
must not make the feature disappear. A failed `Bind`, an exception escaping
module code, or an internal invariant failure instead means the parent can no
longer trust that module instance. Keeping its routes/actions live would let a
faulty child repeatedly destabilize the resident process.

Window lifetime is also shorter than process lifetime. Closing the last app
window must release activated modules and cancel their host lifetime without
terminating the tray-resident parent.

## Decision

1. `InvalidArgument`, `NotFound`, `AlreadyExists`, `IoError`, `ParseError`,
   `PermissionDenied`, `Unsupported`, and `Cancelled` returned from `Handle`
   are operational statuses. The parent records the latest status and leaves
   the module mounted.
2. Any failed `Bind`, any exception crossing `Bind`/`Handle`, and an explicit
   `Internal` result are fatal for that module lifetime. The parent calls
   `Release()` once, destroys its `GateHost`, removes its actions and routes,
   and records a staged runtime diagnostic. Healthy siblings remain mounted.
3. `Release()` runs once for every started bind lifetime: on last-window close,
   quarantine, or final shutdown. Repeated close/shutdown calls are idempotent.
   Destroying `GateHost` invalidates worker delivery and suppresses callbacks
   belonging to that released module lifetime.
4. Diagnostics are parent-owned immutable metadata exposed through
   `ModuleHost::Diagnostics()`. The diagnostics child receives no gate pointer,
   global singleton, route mutator, or platform service.

## Consequences

- User and OS errors remain visible and retryable instead of disabling a
  feature.
- A structurally unhealthy child is removed on first fatal boundary failure,
  while the resident parent and healthy modules continue.
- Reopening a window creates a fresh host lifetime and rebinds the descriptor;
  no completion from the closed lifetime may reach it.
- The read model is bounded by module count and adds no polling, timer, thread,
  or idle wakeup.

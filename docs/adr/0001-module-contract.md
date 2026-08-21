# ADR 0001 — The module contract and parent-owned services

Status: accepted (Phase 3, issue #81). Links: `.docs/plan.md` Part 3.

## Context

dhepz modules are pairs of halves — a UI half (screen JSON) and a logic half
(C++ `ModuleDescriptor`) — joined by `moduleId`. The parent reaches the child
only through `ModuleDescriptor`; the child reaches the world only through
`ModuleHost`. Hyrum's Law applies hard: once a module ships against these
types, every signature and semantic becomes a contract.

## Decision

1. `ModuleHost` and `ModuleDescriptor` (src/modules/contract/module_contract.h)
   are **append-only** after the Integration Checkpoint contract freeze.
   Before any production module path shipped, the synchronous `ProcessRun`
   member was removed as a one-time breaking correction: its signature made
   blocking process work legal on the UI thread. The typed asynchronous
   operations below are its complete replacement. Further removals, signature
   changes, or semantic changes are major-version events and must amend this
   ADR first.
2. Reach beyond the default surface exists only as capabilities declared in
   `module.json` and granted by the gate: `settings:all` (single claimant) and
   `config:write` (writes only through the gate's validate-then-swap path).
   The set is closed; a new capability is a contract change, not a config one.
3. `Peers()` returns inert metadata (`moduleId`, `tabLabel`, `settingsRoute`)
   only — never handles. Sibling calls are structurally impossible.
4. `module.json` is strict: unknown fields are diagnostics, not ignored, so a
   typo can never silently become "supported later".
5. Process reach is parent-owned and structured. A child supplies an executable,
   an argument vector, working directory, operation kind (normal launch,
   elevated launch, or capture), and capture timeout. It never supplies a raw,
   prequoted command line. The parent applies `str::QuoteArg` and is the only
   layer that calls `process::Launch`, `ShellLaunch`, or `RunCapture` for this
   service.
6. Folder reach is a metadata-only probe: the child supplies one absolute
   directory and a list of safe relative file names. Completion reports the
   normalized directory, directory presence, and presence of exactly those
   files. It never exposes file contents or general filesystem access.
7. Starting either operation is nonblocking. A successful start returns a
   parent-issued request token; malformed input is rejected synchronously with
   `Status`. OS work runs on a run-once worker. Completion is posted to the UI
   thread and carries token, host-lifetime generation, operation kind, `Status`,
   and the kind-appropriate result.
8. Cancellation is best-effort for an operation already applying an external
   effect: capture is terminated through its cancellation flag, while a child
   process successfully launched before cancellation is not killed. In every
   case cancellation suppresses delivery. Invalidating the module-host
   generation suppresses queued and future completions, including completions
   already posted when the owner begins teardown.
9. An asynchronous module publishes a view-state patch only through
   `ModuleHost::PublishStatePatch` from its UI-thread completion. The parent
   owns the patch sink and presenter routing; calling the seam from another
   thread is an error.
10. Privileged reach uses two dedicated typed facets. `SettingsAllFacet` is
    granted only to the module whose id is `settings`; it can read and write
    global values and every module section and can enumerate inert peer
    metadata. `ConfigWriteFacet` is granted only to `ui-editor`; it exposes
    Preview, Save, and Discard rather than a filesystem API. A duplicate claim,
    a claim by any other module id, or an undeclared facet request is denied by
    the gate and recorded with module, capability, and source location.
11. A config Preview resolves the complete candidate before changing the live
    immutable document. A successful preview issues a monotonically increasing
    token and reports only changed routes. A newer preview makes older tokens
    stale. Discard swaps back the exact retained document without I/O. Save is
    valid only for the active token and queues an atomic write on a parent
    worker; its completion returns to the UI thread as `ConfigSave`.
12. Settings and config files are parent-owned. Modules never receive a path or
    call filesystem APIs. `AppGate` orchestrates grants and document ownership;
    dedicated parent services implement settings access and config transaction
    state.

## Consequences

- Modules can be added without touching central files (G5) and the contract
  cannot drift per-module.
- The gate (P3-03) is the single enforcement point; nothing else mounts,
  registers, or swaps config.
- Process/folder IO is structurally outside the UI thread, and worker delivery
  state outlives a queued message safely during teardown without retaining a
  worker thread at idle.
- Capability grants and denials are inspectable gate diagnostics. Config
  preview remains exact and revertible, while persistence has one atomic,
  off-UI-thread implementation owned by the parent.
- Cost: adding a genuinely new service means growing `ModuleHost` for all
  modules — deliberately expensive, so it happens rarely and reviewed.

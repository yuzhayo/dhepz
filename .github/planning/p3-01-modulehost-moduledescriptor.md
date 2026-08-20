P3-01 ModuleHost + ModuleDescriptor contract

Scope
- Define the ModuleHost (parent) and ModuleDescriptor (child) API surface.
- ModuleHost provides: Surface, Settings, Storage, Process, Status, Log, RequestRoute, Peers.
- ModuleDescriptor exposes: moduleId, tabLabel, order, showInTabs, settingsRoute?, DeclaredActions, DeclaredBindings, DeclaredCapabilities, Bind, Handle, Release.
- Keep the contract append-only and explicitly documented; breaking changes are expensive once modules ship.

Acceptance criteria
- A ModuleDescriptor validates against the schema and can be consumed by a ModuleHost without referencing siblings or globals.
- The contract rejects descriptors missing required fields or declaring unknown capabilities.
- The contract is documented in code + a short ADR linking to the plan.

Verification
- Unit tests: descriptor validation and minimal host calls (Bind/Handle/Release) succeed for a healthy descriptor and fail with clear diagnostics for malformed ones.
- A small integration test that constructs a descriptor and mounts it into a test host.

Why this first
- The contract is the foundation for Phase 3; everything else depends on it.

Scope: S

Do not start until: Phase 2 gate (#59) is closed and design read (AGENTS.md + .docs/plan.md).
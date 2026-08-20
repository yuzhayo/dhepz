P3-01 ModuleHost + ModuleDescriptor contract (includes module.json schema)

Scope
- Define the ModuleHost (parent) and ModuleDescriptor (child) API surface and the module.json manifest schema used to describe modules.
- ModuleHost provides: Surface, Settings, Storage, Process, Status, Log, RequestRoute, Peers.
- ModuleDescriptor / module.json fields: moduleId, tabLabel, order, showInTabs, settingsRoute?, DeclaredActions, DeclaredBindings, DeclaredCapabilities, Bind, Handle, Release.
- Document capability semantics (single-claim capabilities such as `settings:all`, reserved capabilities such as `config:write`) in the schema and in a short ADR.
- Keep the contract append-only and explicitly documented; breaking changes are expensive once modules ship.

Acceptance criteria
- module.json schema validates descriptors and rejects missing required fields or illegal capabilities.
- A ModuleDescriptor validated by the schema can be consumed by a ModuleHost without referencing siblings or globals.
- The schema and contract are documented in code + an ADR linking to .docs/plan.md.

Verification
- Unit tests: schema validation (good and bad manifests), descriptor validation, and minimal host calls (Bind/Handle/Release) succeed for a healthy descriptor and fail with precise diagnostics for malformed ones.
- Integration test: a generated module.json mounted into a test host results in a live module instance.

Why this first
- The contract and manifest schema are the foundation for Phase 3; everything else depends on them and they must be stable before discovery, gate, or scaffolding are implemented.

Scope: S

Do not start until: Phase 2 gate (#59) is closed and the ADR and schema draft are reviewed (AGENTS.md + .docs/plan.md).
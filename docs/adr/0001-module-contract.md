# ADR 0001 — The module contract is append-only

Status: accepted (Phase 3, issue #81). Links: `.docs/plan.md` Part 3.

## Context

dhepz modules are pairs of halves — a UI half (screen JSON) and a logic half
(C++ `ModuleDescriptor`) — joined by `moduleId`. The parent reaches the child
only through `ModuleDescriptor`; the child reaches the world only through
`ModuleHost`. Hyrum's Law applies hard: once a module ships against these
types, every signature and semantic becomes a contract.

## Decision

1. `ModuleHost` and `ModuleDescriptor` (src/modules/contract/module_contract.h)
   are **append-only**. Adding a member is allowed; removing one, changing a
   signature, or re-semanticising behaviour is a major-version event and must
   amend this ADR first.
2. Reach beyond the default surface exists only as capabilities declared in
   `module.json` and granted by the gate: `settings:all` (single claimant) and
   `config:write` (writes only through the gate's validate-then-swap path).
   The set is closed; a new capability is a contract change, not a config one.
3. `Peers()` returns inert metadata (`moduleId`, `tabLabel`, `settingsRoute`)
   only — never handles. Sibling calls are structurally impossible.
4. `module.json` is strict: unknown fields are diagnostics, not ignored, so a
   typo can never silently become "supported later".

## Consequences

- Modules can be added without touching central files (G5) and the contract
  cannot drift per-module.
- The gate (P3-03) is the single enforcement point; nothing else mounts,
  registers, or swaps config.
- Cost: adding a genuinely new service means growing `ModuleHost` for all
  modules — deliberately expensive, so it happens rarely and reviewed.

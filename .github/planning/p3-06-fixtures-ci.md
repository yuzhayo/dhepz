P3-06 Phase 3 gate verification

Scope
- Consolidate Phase 3 verification and run the gate checklist as required by .docs/plan.md.

Acceptance criteria (gate)
- Three module folders present: one healthy, one typo'd action, one malformed JSON.
- App starts; the healthy module works; the broken ones appear in diagnostics with reasons; nothing else is affected.
- Deleting the healthy folder and rebuilding removes it cleanly with no other file edited.
- CI asserts all of the above, and a regression in degraded mode fails the build.

Verification
- Integration and CI checks defined plainly and run automatically as part of the contract-validation step.

Why this last
- This gate closes Phase 3 and prevents Phase 4 from beginning until the contract is proven.

Scope: M

Do not start until: P3-05 is merged and CI is configured to run the fixture checks.

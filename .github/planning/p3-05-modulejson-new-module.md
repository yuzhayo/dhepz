P3-05 module.json manifest schema and New-Module.ps1

Scope
- Define module.json schema (fields, capabilities, actions, bindings) and the New-Module.ps1 scaffolding script.
- Enforce capability rules in the schema: single-claim capabilities (e.g., settings:all), reserved capabilities (config:write), and documented constraints.

Acceptance criteria
- New-Module.ps1 produces a module folder that passes the descriptor/schema validation.
- Invalid manifests fail the validation with file+line diagnostics.
- The scaffolding script refuses to create a module with a duplicate moduleId or illegal capability.

Verification
- Script test: run New-Module.ps1 -> validate manifest -> build test ensures discovery picks it up.
- Unit tests for manifest validation (missing fields, illegal capabilities, duplicate IDs).

Why this next
- Scaffolding must match the contract so module authors get a correct starting shape.

Scope: S

Do not start until: P3-03 and P3-04 are in review.
P3-02 ModuleRegistry self-registration and discovery

Scope
- Implement ModuleRegistry that discovers module folders and self-registers modules into the host without editing any central file.
- Ensure modules compile into the EXE and self-register; avoid static .lib patterns that the linker can drop.
- Implement a deterministic discovery order and a three-stage scan trigger split by cost.

Acceptance criteria
- Adding a valid module folder and rebuilding makes it available without editing central files.
- Duplicate or conflicting module IDs are rejected with clear diagnostics.
- Broken module folders (malformed JSON, missing entry) are skipped and reported; they do not crash the app.

Verification
- Unit tests for discovery, duplicate detection, and skipped invalid folder behaviour.
- Integration: a test run with two valid module folders and one invalid folder shows the two modules available and the invalid one reported.

Why this next
- Discovery is the layer that exposes modules to the gate; it must be reliable before the gate runs.

Scope: S

Do not start until: P3-01 is implemented and the descriptor schema is stable.
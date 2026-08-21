P4-01 Terminal module scaffold and manifest

Scope
- Create the terminal module folder shape used as the Phase 4 reference module.
- Include module.json manifest, module entrypoints, minimal UI screen JSON (screen id + route), and New-Module-derived scaffold for the terminal.
- Ensure the module builds into the EXE and self-registers with ModuleRegistry.

Acceptance criteria
- The scaffolded module compiles and is discovered by the ModuleRegistry without editing central files.
- module.json is valid and declares NO capabilities: process launch and own-section settings are the default ModuleHost reach, and the capability set is closed per ADR 0001 (only settings:all and config:write exist). Any declared capability beyond those is a contract violation the gate rejects.
- The module folder is self-contained: all code, assets, and manifest live inside it.

Verification
- Unit test: scaffold -> build -> discovery shows the module in the ModuleRegistry.
- Manual check: module folder contains only its own files; no central file edits were required.

Scope: S

Do not start until: Phase 3 gate is closed and P3-01..P3-06 are merged.
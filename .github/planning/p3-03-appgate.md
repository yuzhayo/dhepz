P3-03 AppGate: pair / validate / mount / lazy-activate

Scope
- Implement the AppGate orchestration: ResolveConfig, CollectModules, PairAndValidate, MountAccepted, LazyActivate, Dispatch, ApplyConfig, Peers, Diagnostics surfaces.
- Pair host capabilities with module-declared capabilities and refuse modules with unmet requirements.
- Lazy-activate modules so activation work runs off the UI thread or on first use.

Acceptance criteria
- Healthy modules mount and activate cleanly.
- Modules with unknown capabilities or malformed actions are rejected with a precise diagnostic and do not crash the app.
- The gate preserves app availability while skipping bad modules.

Verification
- Unit tests for capability pairing and rejection paths.
- Integration test: one healthy module + two broken modules scanned together — healthy module available, broken modules reported with reasons.

Why this next
- The gate enforces the contract at runtime; without it, discovery is only a list and acceptance is undefined.

Scope: M

Do not start until: P3-02 is in review.
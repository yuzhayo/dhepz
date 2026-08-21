P4-03 WSL enumeration and caching

Scope
- Implement WSL distro enumeration (`wsl -l -q`) via RunCapture on a worker and cache per session.
- Provide an explicit refresh operation and an invalidation path when the cache is stale.
- Ensure UTF-16/UTF-8 sniffing and correct decoding of captured output.

Acceptance criteria
- WSL enumeration runs on a worker and returns a stable list of distro names.
- Adding a new distro (on the machine) becomes visible after calling the explicit refresh; no rebuild required.
- Enumeration is cached per process session and refreshable via UI.

Verification
- Integration test: mock RunCapture output to return a list and verify the cache + refresh behavior.
- Manual check on a machine with WSL installed shows distro list returned and refresh picks up new ones.

Scope: S

Do not start until: P4-02 has basic RunCapture tests passing.
P4-05 Worker offload: validation, enumeration, and blocking work

Scope
- Ensure all blocking work (RunCapture, folder validation, distro enumeration, heavy IO) runs on the worker pool.
- Add completion callbacks posted to the UI thread with safe marshaling and Status results.
- Instrument the worker tasks so CI can detect if work is accidentally performed on the UI thread.

Acceptance criteria
- No worker-bound function performs blocking IO on the UI thread.
- Callbacks arrive on the UI thread with a Status; failures are handled gracefully.
- Tests or instrumentation assert the worker pool handles the expected load.

Verification
- Unit tests that artificially delay worker tasks and assert the UI thread remains responsive.
- Instrumentation added to the test run to assert no blocking operations on the UI thread.

Scope: M

Do not start until: P4-02 and P4-03 are merged and tested.
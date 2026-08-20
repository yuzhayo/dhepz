# Regenerates the GitHub issues for this phase. Already run once; the issues
# exist. Kept as the template for breaking down Phases 2-7 at their gates.
#
# NOT idempotent: running it again creates duplicates. Close or delete the
# existing issues first, or copy this file and edit the $tasks list.

$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path "$PSScriptRoot\..\..")
$tasks = @()

$tasks += @{
  title = 'P1-01 Worker pool - run-once jobs with completion posted to the UI thread'
  labels = 'phase-1,from-scratch,goal:G2-responsive'
  milestone = 'Phase 1 - Window shell and rendering'
  body = @'
**There is nothing to port here.** A sweep of the old build found zero uses of `std::thread`, `jthread`, `CreateThread`, `QueueUserWorkItem`, `TrySubmitThreadpool*` or `_beginthread` anywhere in `src`. The old code documents the intent and never implemented it — `wsl.cpp:99-102` literally says *"the first probe must happen off any thread that paints... this is a contract, not an assert"*. So this is new work, and it lands in Phase 1 because everything downstream (WSL enumeration, folder validation, process capture) depends on it.

The hard constraint is that G1 says **no threads alive at idle** and G2 says **the UI thread never blocks**. Both, at once. That rules out a persistent thread pool sitting on a condition variable.

**Acceptance criteria**
- [ ] Jobs are **spawned per job and joined**; zero worker threads exist at idle
- [ ] A job returns its result by **posting a message to the UI thread**, never by touching UI state directly — no locks around UI data, because there is no shared UI data
- [ ] Completion posts carry a **generation/epoch stamp**; a result arriving after its window closed or its route changed is dropped, not applied to a stale tree
- [ ] Cancellation: a job whose requester is gone stops as soon as it observes the flag, and its completion post is discarded
- [ ] Shutdown joins or abandons cleanly with no `std::terminate` and no leaked handle
- [ ] Thread creation cost is measured — if per-job spawn turns out to cost more than the responsiveness budget allows, the fallback is a lazily-created pool that **fully drains and exits** after an idle timeout, which still satisfies "no threads at idle". Decide with a number, not a preference
- [ ] Rapid repeated requests (typing in a folder box) coalesce rather than spawning a thread per keystroke

**Verification**
- [ ] Test: a job that sleeps 2 s never delays a paint — verified from the trace, not by eye
- [ ] Test: after all jobs complete, thread count returns to exactly 1
- [ ] Test: a result posted after its epoch was invalidated is dropped
- [ ] Test: 1,000 sequential jobs leave handle count flat
- [ ] Soak: idle CPU still 0.0% after jobs have run

**Why (goals)** G2''s "UI thread never blocks" is currently documented intent with no implementation. This makes it real. G1 constrains how.

**Scope** M — and the highest-risk issue in Phase 1, which is why it is first.
'@
}

$tasks += @{
  title = 'P1-02 RenderBackend interface - the Direct2D seam'
  labels = 'phase-1,goal:G2-responsive'
  milestone = 'Phase 1 - Window shell and rendering'
  body = @'
Define the rendering interface before writing the GDI implementation behind it. GDI is the Phase 1 choice because it is simple and starts fast; the plan says it is revisited **only if it misses responsiveness targets**, not because Direct2D is fashionable. The seam is what makes that revisit a contained change instead of a rewrite.

**Acceptance criteria**
- [ ] `render_backend.h` declares the full drawing surface the UI needs: filled and stroked rects, rounded rects, text with measurement, images, clipping, and transforms if needed
- [ ] Text **measurement** is part of the interface, since layout depends on it and it must work without a live paint scope
- [ ] No GDI type (`HDC`, `HBRUSH`, `HFONT`, ...) appears in the header — if it does, the seam does not exist
- [ ] Resource handles are opaque and backend-owned, referenced through the cache by key
- [ ] The interface is written down as a real contract: who owns lifetime, what is valid inside vs outside a paint scope, what happens on DPI change
- [ ] Hyrum''s Law note recorded in the header: once components are written against this, changing it touches all 14

**Verification**
- [ ] `ui/` compiles with the GDI implementation excluded from the build and a stub substituted — proof the seam holds
- [ ] Grep confirms no Win32 GDI type in `render_backend.h`

**Why (goals)** G2. This is the escape hatch if GDI cannot hit the frame budget, and it is only cheap while it is still theoretical.

**Scope** S (design-heavy, little code)
'@
}

$tasks += @{
  title = 'P1-03 GDI backend, per-window back buffer, and paint-scope discipline'
  labels = 'phase-1,ported,goal:G2-responsive'
  milestone = 'Phase 1 - Window shell and rendering'
  body = @'
Implement `RenderBackend` over GDI, ported from the old `render_runtime` and compositor. Double-buffered into a DIB so there is no flicker and no partial frame.

**Acceptance criteria**
- [ ] Per-window back buffer as a DIB section, sized to the client rect, resized on `WM_SIZE`
- [ ] Everything draws into the back buffer, then one `BitBlt` to the window
- [ ] `WM_ERASEBKGND` returns 1 — suppressing it is what removes the background flash
- [ ] `MeasurementDc()` **refuses to create a DC inside a paint scope** and asserts loudly (old build: `render_runtime.cpp:112-115`). This caught real bugs; keep the guard
- [ ] DPI-aware: fonts and metrics scale from the window''s DPI, not the system''s
- [ ] Every GDI object created is released; no `DeleteObject` omitted on any error path

**Verification**
- [ ] 100 open/close cycles: GDI and USER handle counts flat (`GetGuiResources`)
- [ ] Resize by dragging: frame time ≤ 16.7 ms with no stall > 50 ms, from the trace
- [ ] No visible flicker on show, resize, or theme change
- [ ] Attempting a measurement DC inside a paint scope fails a test

**Scope** M
'@
}

$tasks += @{
  title = 'P1-04 Dirty-rect painting with TakeInvalidation'
  labels = 'phase-1,ported,goal:G2-responsive'
  milestone = 'Phase 1 - Window shell and rendering'
  body = @'
Repaint only what changed. Ported pattern. A full-window repaint on every keystroke is the difference between a UI that feels immediate and one that feels laggy at larger window sizes.

**Acceptance criteria**
- [ ] Components accumulate invalid regions; `TakeInvalidation` atomically takes and clears the pending region
- [ ] Multiple invalidations between frames coalesce into one paint
- [ ] Paint clips to the dirty region, so untouched pixels cost nothing
- [ ] An invalidation while no window is visible does **not** trigger a paint or arm a timer (G1)
- [ ] A caret blink or hover effect invalidates only its own bounds

**Verification**
- [ ] Trace: typing one character produces one paint whose dirty rect is the text field, not the window
- [ ] Input-to-paint ≤ 16 ms from the trace correlation IDs
- [ ] Ten invalidations in one message-loop iteration produce one paint

**Scope** M
'@
}

$tasks += @{
  title = 'P1-05 Three-layer GDI object cache with resource-epoch invalidation'
  labels = 'phase-1,ported,goal:G1-lightweight'
  milestone = 'Phase 1 - Window shell and rendering'
  body = @'
Fonts, brushes, pens and corner tiles are expensive to create and cheap to keep. The plan explicitly says these **persist across window close** — dropping them means re-rendering every rounded corner on the next show, which is exactly the latency the cache exists to remove. The catch is that anything kept must be bounded, or it becomes drift.

**Acceptance criteria**
- [ ] Three layers as in the old build: process-lifetime, theme-scoped, and window-scoped
- [ ] Every cache entry is keyed and every cache has a **bound**; an unbounded cache is a leak with good manners
- [ ] A **resource epoch** bumps on theme change or DPI change, invalidating the affected layers in one operation rather than by hunting individual entries
- [ ] Cache sizes are reported in `ResourceSnapshot` (P0-10), so drift is visible in measurement rather than discovered at hour six
- [ ] Window-scoped entries are released on window destroy; theme and process layers survive

**Verification**
- [ ] 100 open/close cycles: handle count flat, cache sizes return to their steady-state values
- [ ] Theme toggled 50 times: no growth in handles or private bytes
- [ ] Snapshot shows every cache''s size and each stays under its bound

**Why (goals)** G1 for the bound, G2 for keeping the cache at all. This issue is where the two goals meet, and G2 wins the tie.

**Scope** M
'@
}

$tasks += @{
  title = 'P1-06 Window shell - frame, chrome, and show/hide lifecycle'
  labels = 'phase-1,goal:G2-responsive'
  milestone = 'Phase 1 - Window shell and rendering'
  body = @'
One real window with a custom frame. Routing, tabs and components come in Phase 2 — this issue is the container and, more importantly, the lifecycle rules that the responsiveness budget depends on.

**Acceptance criteria**
- [ ] Custom-drawn frame: caption, minimise/maximise/close, resize borders, drag-to-move, snap behaviour intact
- [ ] Rounded corners and shadow consistent with the old build''s look — visual quality is part of G2, not a nice-to-have
- [ ] **On hide or close: the DIB and window-scoped GDI objects are released** (old build: `window_container.cpp:712-718,690-710,648-674`). This is the one large allocation and it scales with window size
- [ ] **On restore: a full frame is rendered offscreen *before* `ShowWindow`**, so the window never appears empty and then fill in
- [ ] Multi-monitor and per-monitor DPI change handled without a stale-size frame
- [ ] Closing the last window returns the process to the tray-resident state, not to exit

**Verification**
- [ ] Hide/restore 100×: handle count and private bytes flat
- [ ] Warm show measured from the trace against the Part 1 budget
- [ ] Moving the window between monitors with different DPI produces no blurry or wrongly-sized frame
- [ ] No frame is ever visible in a partially-painted state (recorded video or trace ordering)

**Scope** L — consider splitting frame drawing from lifecycle if it grows past ~5 files.
'@
}

$tasks += @{
  title = 'P1-07 Coalesced OS-signal fan-out'
  labels = 'phase-1,ported,goal:G1-lightweight'
  milestone = 'Phase 1 - Window shell and rendering'
  body = @'
Ported from `application_infrastructure_window.cpp:213-227`. The OS delivers bursts of broadcast signals — theme change, DPI change, settings change, session change — and naively forwarding each to every window turns one system event into an N×M storm of work.

**Acceptance criteria**
- [ ] Pending signals accumulate in a **bitmask**, and exactly **one** `PostMessage` is issued regardless of how many arrive
- [ ] The drain uses `std::exchange` to take-and-clear atomically, so a signal arriving mid-drain is not lost
- [ ] **If the post fails, drain synchronously** — the old code does this deliberately; dropping the signal silently is worse than doing the work inline
- [ ] Zero cost when no window is open: signals are recorded and applied on next show, not processed immediately
- [ ] No timer involved anywhere in the mechanism

**Verification**
- [ ] Test: 100 rapid theme-change broadcasts produce one fan-out
- [ ] Test: a simulated `PostMessage` failure still applies the signal
- [ ] Trace: a theme change with no window open performs no paint and creates no GDI object

**Scope** S
'@
}

$tasks += @{
  title = 'P1-08 Settle timer - armed on activity, killed on fire'
  labels = 'phase-1,ported,goal:G1-lightweight'
  milestone = 'Phase 1 - Window shell and rendering'
  body = @'
Ported from `window_container.cpp:1568-1574` and `:1472-1480`. This resolves an apparent contradiction in the plan: the app must measure "time until things settle after a resize", but G1 forbids timers at idle. The answer is a timer that only exists while something is happening.

**Acceptance criteria**
- [ ] `SetTimer` is called **only** in response to real activity (resize, DPI change, animation)
- [ ] The handler **calls `KillTimer` as its first action**, before doing any work — so the timer is one-shot by construction, not by hoping the logic reaches the kill
- [ ] Re-arming during ongoing activity resets rather than stacking timers
- [ ] At idle: `GetGuiResources` and a timer audit both show **no timer armed**
- [ ] Every `SetTimer` in the codebase follows this pattern; a review checklist item, not just this one site

**Verification**
- [ ] Automated check: after 60 s idle following a resize storm, no timer is armed
- [ ] Test: 100 resize events do not accumulate timers
- [ ] Idle CPU 0.0% after activity has ceased

**Why (goals)** G1''s "no timers that tick when nothing changed", without giving up settle-time measurement.

**Scope** XS but easy to get subtly wrong; the kill-first ordering is the whole trick.
'@
}

$tasks += @{
  title = 'CHECKPOINT Phase 1 gate'
  labels = 'phase-1,checkpoint'
  milestone = 'Phase 1 - Window shell and rendering'
  body = @'
Phase 2 does not start until every box here is ticked. Phase 1 is where the responsiveness story is either true or not; finding out later means unwinding the component model.

- [ ] A window opens, draws its frame, resizes, moves between monitors, hides and restores
- [ ] **100 open/close cycles with flat handle and RSS counts** — the headline gate
- [ ] Hide/restore 100× also flat, with the DIB genuinely released on hide and a full frame rendered offscreen before each show
- [ ] Idle CPU 0.0%, exactly one thread, **no timer armed at idle** after a resize storm
- [ ] A blocking 2 s job posted to a worker never stalls a paint, verified from the trace
- [ ] After all jobs finish, thread count is back to 1
- [ ] Input-to-paint ≤ 16 ms and resize frames ≤ 16.7 ms with no stall > 50 ms, measured on the installed Release build
- [ ] Warm show measured and within budget
- [ ] `ui/` compiles against a stub backend, proving the Direct2D seam is real
- [ ] `ResourceSnapshot` reports every cache size and each is bounded
- [ ] Visual review: frame, corners and shadow look right. G2 includes appearance
- [ ] If GDI misses any frame-time target, decide **now** whether to take the Direct2D path — the seam is cheapest to use before 14 components exist
'@
}

foreach ($t in $tasks) {
  $f = New-TemporaryFile
  [System.IO.File]::WriteAllText($f.FullName, ($t.body -replace "`r`n", "`n"))
  $url = gh issue create --title $t.title --body-file $f.FullName --label $t.labels --milestone $t.milestone
  Write-Host "$url  $($t.title)"
  Remove-Item $f.FullName -Force
}

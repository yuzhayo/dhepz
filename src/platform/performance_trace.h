// Measurement backbone: an ETW TraceLogging provider plus the resource
// snapshot every leak test asserts on.
//
// Without this, every budget in Part 1 of the plan is unfalsifiable and
// G1/G2 are vibes. The design constraint is G1 itself: the provider costs
// **nothing when no session is listening** — no logging thread, no file IO,
// no idle wakeup. Every event function checks one bool and returns; the
// TraceLogging machinery only runs when a session is attached.
//
// Startup is measured as named milestones, ProcessEntry through
// FirstFrameVisible, each carrying a QueryPerformanceCounter timestamp.
// Contract for the caller of TraceFirstFrameVisible: call it only after
// DwmFlush() has returned, so "visible" means actually composited rather
// than merely submitted.
//
// Input-to-paint and route-switch latencies come from correlation-ID pairs:
// the event that receives the input and the event that presents its visual
// carry the same ID, so a trace consumer can match them.
//
// Like all of platform/, this layer keeps windows.h out of the header.
#pragma once

#include <cstddef>
#include <cstdint>

namespace trace {

// Registers the provider and emits ProcessEntry. Exactly one session exists
// per process, created at startup and destroyed at exit.
class PerformanceTraceSession final {
 public:
  explicit PerformanceTraceSession(std::int64_t process_entry_qpc) noexcept;
  ~PerformanceTraceSession();

  PerformanceTraceSession(const PerformanceTraceSession&) = delete;
  PerformanceTraceSession& operator=(const PerformanceTraceSession&) = delete;

 private:
  bool registered_ = false;
};

// True while a session holds a registered provider. The event functions use
// this as their one-instruction gate.
bool TraceActive() noexcept;

// QueryPerformanceCounter value, for callers that capture a timestamp at an
// event the trace itself does not own (e.g. the very first line of wWinMain).
std::int64_t CurrentQpc() noexcept;

// Startup milestones, in the order they fire.
void TraceConfigResolved() noexcept;
void TraceRenderBufferReady() noexcept;
void TraceFirstLayoutComplete() noexcept;
void TraceFirstPresentComplete() noexcept;
// Call only after DwmFlush() has returned — see the header comment.
void TraceFirstFrameVisible() noexcept;

// Correlation-ID pairs: both halves of each pair carry the same ID.
void TraceInputReceived(std::uint64_t correlation_id) noexcept;
void TraceInputVisualPresented(std::uint64_t correlation_id) noexcept;
void TraceNavigationRequested(std::uint64_t correlation_id) noexcept;
void TraceNavigationPresented(std::uint64_t correlation_id) noexcept;
void TraceResizeFramePresented(std::uint64_t correlation_id) noexcept;
void TraceScenarioSettled(std::uint64_t correlation_id) noexcept;

// One struct carries everything a leak test asserts on, so a soak can
// compare snapshots instead of scraping counters from five APIs.
struct ResourceSnapshot {
  struct CacheCounter {
    const wchar_t* name = L"";
    std::uint64_t size = 0;
  };
  // Bounded by construction: an unbounded counter list would itself be drift.
  static constexpr std::size_t kMaxCaches = 8;

  std::uint64_t private_bytes = 0;
  std::uint64_t working_set_bytes = 0;
  std::uint32_t gdi_objects = 0;
  std::uint32_t user_objects = 0;
  std::uint32_t hwnd_count = 0;
  CacheCounter caches[kMaxCaches];
  std::size_t cache_count = 0;
};

// Fills the OS half of a snapshot for the current process: private bytes
// and working set, GDI/USER object counts, and the number of windows owned
// by this process (top-level plus children). The cache half is filled by
// the caller that owns the caches. Pure measurement — emits nothing.
ResourceSnapshot CaptureResourceSnapshot() noexcept;

// Emits a snapshot as one ResourceSnapshot event. No-op unless a session is
// active. Cache slots beyond cache_count are emitted as empty/zero so the
// event shape is fixed and a consumer never parses a variable field list.
void TraceResourceSnapshot(const ResourceSnapshot& snapshot) noexcept;

}  // namespace trace

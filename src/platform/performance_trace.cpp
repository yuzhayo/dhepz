#include "platform/performance_trace.h"

#include <windows.h>
#include <psapi.h>
#include <TraceLoggingProvider.h>

// A fresh GUID, not the old Terminal's: two providers registered with the
// same ID would collide if both binaries ever ran side by side.
TRACELOGGING_DEFINE_PROVIDER(
    g_dhepz_performance_provider, "Yuzha.Dhepz.Performance",
    (0x8f2c41d6, 0x9b7a, 0x4e53, 0xa1, 0xc8, 0x3d, 0x6e, 0xf5, 0x2b, 0x9a, 0x04));

namespace trace {
namespace {

bool g_registered = false;

struct WindowCounter {
  DWORD process_id = 0;
  std::uint32_t count = 0;
};

BOOL CALLBACK CountChildWindow(HWND, LPARAM value) noexcept {
  auto* counter = reinterpret_cast<WindowCounter*>(value);
  ++counter->count;
  return TRUE;
}

BOOL CALLBACK CountTopLevelWindow(HWND window, LPARAM value) noexcept {
  auto* counter = reinterpret_cast<WindowCounter*>(value);
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id != counter->process_id) {
    return TRUE;
  }
  ++counter->count;
  EnumChildWindows(window, CountChildWindow, value);
  return TRUE;
}

const wchar_t* CacheName(const ResourceSnapshot& snapshot, std::size_t index) {
  return index < snapshot.cache_count ? snapshot.caches[index].name : L"";
}

std::uint64_t CacheSize(const ResourceSnapshot& snapshot, std::size_t index) {
  return index < snapshot.cache_count ? snapshot.caches[index].size : 0;
}

}  // namespace

PerformanceTraceSession::PerformanceTraceSession(std::int64_t process_entry_qpc) noexcept {
  registered_ = TraceLoggingRegister(g_dhepz_performance_provider) == ERROR_SUCCESS;
  g_registered = registered_;
  if (!registered_) {
    return;
  }
  TraceLoggingWrite(g_dhepz_performance_provider, "ProcessEntry",
                    TraceLoggingInt64(process_entry_qpc, "Qpc"));
}

PerformanceTraceSession::~PerformanceTraceSession() {
  if (registered_) {
    g_registered = false;
    TraceLoggingUnregister(g_dhepz_performance_provider);
  }
}

bool TraceActive() noexcept { return g_registered; }

std::int64_t CurrentQpc() noexcept {
  LARGE_INTEGER counter{};
  return QueryPerformanceCounter(&counter) ? counter.QuadPart : 0;
}

void TraceConfigResolved() noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "ConfigResolved",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"));
  }
}

void TraceRenderBufferReady() noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "RenderBufferReady",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"));
  }
}

void TraceFirstLayoutComplete() noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "FirstLayoutComplete",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"));
  }
}

void TraceFirstPresentComplete() noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "FirstPresentComplete",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"));
  }
}

void TraceFirstFrameVisible() noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "FirstFrameVisible",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"));
  }
}

void TraceInputReceived(std::uint64_t correlation_id) noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "InputReceived",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"),
                      TraceLoggingUInt64(correlation_id, "CorrelationId"));
  }
}

void TraceInputVisualPresented(std::uint64_t correlation_id) noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "InputVisualPresented",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"),
                      TraceLoggingUInt64(correlation_id, "CorrelationId"));
  }
}

void TraceNavigationRequested(std::uint64_t correlation_id) noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "NavigationRequested",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"),
                      TraceLoggingUInt64(correlation_id, "CorrelationId"));
  }
}

void TraceNavigationPresented(std::uint64_t correlation_id) noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "NavigationPresented",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"),
                      TraceLoggingUInt64(correlation_id, "CorrelationId"));
  }
}

void TraceResizeFramePresented(std::uint64_t correlation_id) noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "ResizeFramePresented",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"),
                      TraceLoggingUInt64(correlation_id, "CorrelationId"));
  }
}

void TraceScenarioSettled(std::uint64_t correlation_id) noexcept {
  if (g_registered) {
    TraceLoggingWrite(g_dhepz_performance_provider, "ScenarioSettled",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"),
                      TraceLoggingUInt64(correlation_id, "CorrelationId"));
  }
}

ResourceSnapshot CaptureResourceSnapshot() noexcept {
  ResourceSnapshot snapshot;

  // K32-prefixed form: identical behaviour, exported by kernel32, so the
  // layer adds no new import library to the project.
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof(memory);
  if (K32GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                              sizeof(memory))) {
    snapshot.private_bytes = memory.PrivateUsage;
    snapshot.working_set_bytes = memory.WorkingSetSize;
  }

  const HANDLE process = GetCurrentProcess();
  snapshot.gdi_objects = GetGuiResources(process, GR_GDIOBJECTS);
  snapshot.user_objects = GetGuiResources(process, GR_USEROBJECTS);

  WindowCounter windows{GetCurrentProcessId(), 0};
  EnumWindows(CountTopLevelWindow, reinterpret_cast<LPARAM>(&windows));
  snapshot.hwnd_count = windows.count;

  return snapshot;
}

void TraceResourceSnapshot(const ResourceSnapshot& snapshot) noexcept {
  if (!g_registered) {
    return;
  }
  TraceLoggingWrite(
      g_dhepz_performance_provider, "ResourceSnapshot",
      TraceLoggingInt64(CurrentQpc(), "Qpc"),
      TraceLoggingUInt64(snapshot.private_bytes, "PrivateUsageBytes"),
      TraceLoggingUInt64(snapshot.working_set_bytes, "WorkingSetBytes"),
      TraceLoggingUInt32(snapshot.gdi_objects, "GdiObjects"),
      TraceLoggingUInt32(snapshot.user_objects, "UserObjects"),
      TraceLoggingUInt32(snapshot.hwnd_count, "HwndCount"),
      TraceLoggingWideString(CacheName(snapshot, 0), "CacheName0"),
      TraceLoggingUInt64(CacheSize(snapshot, 0), "CacheSize0"),
      TraceLoggingWideString(CacheName(snapshot, 1), "CacheName1"),
      TraceLoggingUInt64(CacheSize(snapshot, 1), "CacheSize1"),
      TraceLoggingWideString(CacheName(snapshot, 2), "CacheName2"),
      TraceLoggingUInt64(CacheSize(snapshot, 2), "CacheSize2"),
      TraceLoggingWideString(CacheName(snapshot, 3), "CacheName3"),
      TraceLoggingUInt64(CacheSize(snapshot, 3), "CacheSize3"),
      TraceLoggingWideString(CacheName(snapshot, 4), "CacheName4"),
      TraceLoggingUInt64(CacheSize(snapshot, 4), "CacheSize4"),
      TraceLoggingWideString(CacheName(snapshot, 5), "CacheName5"),
      TraceLoggingUInt64(CacheSize(snapshot, 5), "CacheSize5"),
      TraceLoggingWideString(CacheName(snapshot, 6), "CacheName6"),
      TraceLoggingUInt64(CacheSize(snapshot, 6), "CacheSize6"),
      TraceLoggingWideString(CacheName(snapshot, 7), "CacheName7"),
      TraceLoggingUInt64(CacheSize(snapshot, 7), "CacheSize7"));
}

}  // namespace trace

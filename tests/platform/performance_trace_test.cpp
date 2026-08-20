#include "platform/performance_trace.h"

#include <windows.h>

#include "framework/test_case.h"

DHEPZ_TEST(Trace, SessionLifecycleGatesTheEvents) {
  DHEPZ_CHECK_FALSE(trace::TraceActive());

  {
    trace::PerformanceTraceSession session(trace::CurrentQpc());
    DHEPZ_CHECK(trace::TraceActive());

    // Every event function must be safe with a live session...
    trace::TraceConfigResolved();
    trace::TraceRenderBufferReady();
    trace::TraceFirstLayoutComplete();
    trace::TraceFirstPresentComplete();
    trace::TraceFirstFrameVisible();
    trace::TraceInputReceived(1);
    trace::TraceInputVisualPresented(1);
    trace::TraceNavigationRequested(2);
    trace::TraceNavigationPresented(2);
    trace::TraceResizeFramePresented(3);
    trace::TraceScenarioSettled(3);
    trace::TraceResourceSnapshot(trace::CaptureResourceSnapshot());
  }

  // ...and after the session is gone the gate is closed again.
  DHEPZ_CHECK_FALSE(trace::TraceActive());
  trace::TraceConfigResolved();
  trace::TraceInputReceived(9);
  trace::TraceResourceSnapshot(trace::CaptureResourceSnapshot());
}

// The criterion: snapshot counters match a known-good manual reading of the
// same APIs, taken back-to-back so nothing else can move between them.
DHEPZ_TEST(Trace, SnapshotMatchesManualCounters) {
  const trace::ResourceSnapshot snapshot = trace::CaptureResourceSnapshot();
  const HANDLE process = GetCurrentProcess();

  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(snapshot.gdi_objects),
                 static_cast<unsigned long long>(GetGuiResources(process, GR_GDIOBJECTS)));
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(snapshot.user_objects),
                 static_cast<unsigned long long>(GetGuiResources(process, GR_USEROBJECTS)));
  DHEPZ_CHECK(snapshot.private_bytes > 0);
  DHEPZ_CHECK(snapshot.working_set_bytes > 0);
}

DHEPZ_TEST(Trace, SnapshotSeesGdiObjectsComeAndGo) {
  const trace::ResourceSnapshot before = trace::CaptureResourceSnapshot();

  HFONT fonts[3] = {};
  for (int i = 0; i < 3; ++i) {
    fonts[i] = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                           DEFAULT_PITCH, L"dhepz trace test");
    DHEPZ_CHECK(fonts[i] != nullptr);
  }
  const trace::ResourceSnapshot with_fonts = trace::CaptureResourceSnapshot();
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(with_fonts.gdi_objects - before.gdi_objects),
                 static_cast<unsigned long long>(3));

  for (HFONT font : fonts) {
    DeleteObject(font);
  }
  const trace::ResourceSnapshot after = trace::CaptureResourceSnapshot();
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(after.gdi_objects),
                 static_cast<unsigned long long>(before.gdi_objects));
}

DHEPZ_TEST(Trace, SnapshotCountsThisProcessWindowsOnly) {
  const trace::ResourceSnapshot before = trace::CaptureResourceSnapshot();

  WNDCLASSW window_class{};
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"dhepz_trace_test_window";
  DHEPZ_CHECK(RegisterClassW(&window_class) != 0);
  HWND window = CreateWindowExW(0, window_class.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16,
                                nullptr, nullptr, window_class.hInstance, nullptr);
  DHEPZ_CHECK(window != nullptr);

  const trace::ResourceSnapshot with_window = trace::CaptureResourceSnapshot();
  // At least the window just created. Windows may add an auxiliary window of
  // its own (input method machinery) on a process's first window, so the
  // delta is "grew" rather than "exactly one" — what matters is that only
  // this process's windows are counted and that they all go away again.
  DHEPZ_CHECK(with_window.hwnd_count > before.hwnd_count);

  DestroyWindow(window);
  UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
  const trace::ResourceSnapshot after = trace::CaptureResourceSnapshot();
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(after.hwnd_count),
                 static_cast<unsigned long long>(before.hwnd_count));
}

DHEPZ_TEST(Trace, SnapshotCarriesBoundedCacheCounters) {
  trace::ResourceSnapshot snapshot = trace::CaptureResourceSnapshot();
  snapshot.caches[0] = {L"fonts", 12};
  snapshot.caches[1] = {L"brushes", 3};
  snapshot.cache_count = 2;

  // Emitting a populated snapshot must be safe with or without a session.
  trace::TraceResourceSnapshot(snapshot);
  {
    trace::PerformanceTraceSession session(trace::CurrentQpc());
    trace::TraceResourceSnapshot(snapshot);
  }
  DHEPZ_CHECK_EQ(trace::ResourceSnapshot::kMaxCaches, static_cast<std::size_t>(8));
}

DHEPZ_TEST(Trace, QpcIsMonotonic) {
  const std::int64_t first = trace::CurrentQpc();
  const std::int64_t second = trace::CurrentQpc();
  DHEPZ_CHECK(first > 0);
  DHEPZ_CHECK(second >= first);
}

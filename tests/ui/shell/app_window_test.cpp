#include "ui/shell/app_window.h"

#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <thread>

#include "framework/test_case.h"
#include "render/gdi_backend.h"
#include "render/gdi_resource_cache.h"

namespace {

void* TestInstance() { return GetModuleHandleW(nullptr); }

// The shell runs on a message loop in the real app; tests pump briefly when
// a step posts messages (minimise, resize).
void PumpFor(int milliseconds) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < deadline) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace

DHEPZ_TEST(AppWindow, ShowRendersBeforeVisibleAndHideReleases) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
  DHEPZ_CHECK_FALSE(window.visible());
  DHEPZ_CHECK_EQ(window.backend()->buffer_width(), 0);

  window.Show();
  DHEPZ_CHECK(window.visible());
  DHEPZ_CHECK(window.backend()->buffer_width() > 0);
  // The frame was painted: the content centre is opaque, not empty.
  const int cx = window.backend()->buffer_width() / 2;
  const int cy = window.backend()->buffer_height() / 2;
  DHEPZ_CHECK((window.backend()->PixelAt(cx, cy) >> 24) == 0xFF);

  window.Hide();
  DHEPZ_CHECK_FALSE(window.visible());
  DHEPZ_CHECK(window.alive());  // resident: the window object survives
  DHEPZ_CHECK_EQ(window.backend()->buffer_width(), 0);  // the DIB is gone

  window.Show();
  DHEPZ_CHECK(window.visible());
  DHEPZ_CHECK(window.backend()->buffer_width() > 0);
}

DHEPZ_TEST(AppWindow, CloseReturnsToResidentNotExit) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
  window.Show();
  window.Close();
  DHEPZ_CHECK_FALSE(window.visible());
  DHEPZ_CHECK(window.alive());
  DHEPZ_CHECK_EQ(window.backend()->buffer_width(), 0);
}

DHEPZ_TEST(AppWindow, MinimiseReleasesAndRestoreRebuilds) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
  window.Show();
  PumpFor(30);

  ShowWindow(static_cast<HWND>(window.hwnd()), SW_MINIMIZE);
  PumpFor(60);
  DHEPZ_CHECK_EQ(window.backend()->buffer_width(), 0);

  ShowWindow(static_cast<HWND>(window.hwnd()), SW_RESTORE);
  PumpFor(60);
  DHEPZ_CHECK(window.visible());
  DHEPZ_CHECK(window.backend()->buffer_width() > 0);
}

DHEPZ_TEST(AppWindow, HundredHideShowCyclesStayFlat) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
  window.Show();
  PumpFor(30);

  const HANDLE process = GetCurrentProcess();
  const DWORD handles_before = GetGuiResources(process, GR_GDIOBJECTS);
  PROCESS_MEMORY_COUNTERS_EX memory_before{};
  memory_before.cb = sizeof(memory_before);
  K32GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory_before),
                          sizeof(memory_before));

  for (int i = 0; i < 100; ++i) {
    window.Hide();
    window.Show();
  }
  PumpFor(60);

  const DWORD handles_after = GetGuiResources(process, GR_GDIOBJECTS);
  PROCESS_MEMORY_COUNTERS_EX memory_after{};
  memory_after.cb = sizeof(memory_after);
  K32GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory_after),
                          sizeof(memory_after));

  // Flat within a couple of handles: a shrinking count is fine (no leak);
  // a growing one is the failure this test hunts.
  const long long handle_drift = static_cast<long long>(handles_after) -
                                 static_cast<long long>(handles_before);
  DHEPZ_CHECK(handle_drift <= 2);
  // PrivateUsage, not working set: the buffer is allocated and freed every
  // cycle, and only committed memory is the honest drift signal.
  const long long drift = static_cast<long long>(memory_after.PrivateUsage) -
                          static_cast<long long>(memory_before.PrivateUsage);
  DHEPZ_CHECK(drift < 512 * 1024);
}

DHEPZ_TEST(AppWindow, DpiChangeRebuildsWithoutAStaleFrame) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
  window.Show();
  PumpFor(30);
  const int width_at_96 = window.backend()->buffer_width();

  window.OnDpiChanged(192.0f, 800, 600);
  PumpFor(30);
  DHEPZ_CHECK_EQ(window.backend()->dpi(), 192.0f);
  // The buffer tracks the suggested physical size exactly — no stale size.
  DHEPZ_CHECK_EQ(window.backend()->buffer_width(), 800);
  DHEPZ_CHECK_EQ(window.backend()->buffer_height(), 600);

  RECT rect{};
  GetWindowRect(static_cast<HWND>(window.hwnd()), &rect);
  DHEPZ_CHECK_EQ(static_cast<long long>(rect.right - rect.left), static_cast<long long>(800));
  (void)width_at_96;
}

DHEPZ_TEST(AppWindow, HitTestMapsFrameZones) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance()));
  window.Show();

  // Window is 1008x688 physical at 96 DPI: 24 px shadow margin around a
  // 960x640 content area; caption is the top 40 px of the content.
  DHEPZ_CHECK_EQ(window.HitTest(5, 300), HTTRANSPARENT);    // shadow margin
  DHEPZ_CHECK_EQ(window.HitTest(26, 300), HTLEFT);          // left resize border
  DHEPZ_CHECK_EQ(window.HitTest(500, 26), HTTOP);           // top resize border
  DHEPZ_CHECK_EQ(window.HitTest(100, 40), HTCAPTION);       // caption: drag
  DHEPZ_CHECK_EQ(window.HitTest(500, 300), HTCLIENT);       // content
  DHEPZ_CHECK_EQ(window.HitTest(970, 40), HTCLIENT);        // close button area
}

DHEPZ_TEST(AppWindow, FontsSurviveWindowCloseThroughTheSharedCache) {
  render::GdiResourceCache cache;

  {
    shell::AppWindow window(&cache);
    DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
    window.Show();
    PumpFor(30);
  }  // destroyed: window layer gone, theme layer (fonts) survives
  const std::size_t fonts_after_close = cache.Sizes().fonts;
  DHEPZ_CHECK(fonts_after_close > 0);

  {
    shell::AppWindow window(&cache);
    DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
    window.Show();
    PumpFor(30);
    // No new fonts: the second window reuses what the first warmed.
    DHEPZ_CHECK_EQ(static_cast<unsigned long long>(cache.Sizes().fonts),
                   static_cast<unsigned long long>(fonts_after_close));
  }
}

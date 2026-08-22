#include "ui/app_window/app_window.h"

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
// a step posts resize messages.
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

DHEPZ_TEST(AppWindow, CloseDestroysWindowAndAllowsRecreation) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
  window.Show();
  window.Close();
  DHEPZ_CHECK_FALSE(window.visible());
  DHEPZ_CHECK_FALSE(window.alive());
  DHEPZ_CHECK_EQ(window.backend()->buffer_width(), 0);

  DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
  window.Show();
  DHEPZ_CHECK(window.alive());
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

DHEPZ_TEST(AppWindow, WarmShowFitsTheBudget) {
  // The Part 1 budget: warm show (resident, hidden) to visible ≤ 120 ms p95.
  // Proxy until the app integration wires the window into the resident
  // process: the shell's own Show() from hidden — buffer rebuild, full
  // offscreen frame, present, ShowWindow.
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance()));  // default 960x640 content
  window.Show();
  PumpFor(30);
  window.Hide();

  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER start{};
  QueryPerformanceCounter(&start);
  window.Show();
  LARGE_INTEGER end{};
  QueryPerformanceCounter(&end);
  const double ms = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
                    static_cast<double>(frequency.QuadPart);
  DHEPZ_CHECK(window.visible());
#ifdef NDEBUG
  DHEPZ_CHECK(ms < 120.0);  // the Part 1 warm-show budget
#else
  // Debug runs the pixel loops unoptimised; the budget is asserted in Release.
  DHEPZ_CHECK(ms < 2000.0);
#endif
}

namespace {

// Z-order rank from the top of the desktop: lower means closer to the front.
// Topmost windows walk before non-topmost ones, which is exactly what the
// pin must buy.
int ZRank(HWND target) {
  int rank = 0;
  for (HWND w = GetTopWindow(nullptr); w != nullptr; w = GetWindow(w, GW_HWNDNEXT)) {
    if (w == target) return rank;
    ++rank;
  }
  return -1;
}

HWND MakePlainPopup() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"dhepz.test.pin.peer";
  RegisterClassW(&wc);  // already-exists is fine
  const HWND window = CreateWindowExW(0, wc.lpszClassName, L"peer", WS_POPUP, 0, 0, 120, 90,
                                      nullptr, nullptr, wc.hInstance, nullptr);
  ShowWindow(window, SW_SHOW);
  return window;
}

}  // namespace

DHEPZ_TEST(AppWindow, PinTogglesTopmostAndSurvivesHideShow) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
  window.Show();
  PumpFor(30);
  DHEPZ_CHECK_FALSE(window.pinned());

  const HWND peer = MakePlainPopup();
  SetWindowPos(peer, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  PumpFor(30);

  window.TogglePin();
  PumpFor(30);
  DHEPZ_CHECK(window.pinned());
  DHEPZ_CHECK(ZRank(static_cast<HWND>(window.hwnd())) >= 0);
  DHEPZ_CHECK(ZRank(peer) >= 0);
  DHEPZ_CHECK(ZRank(static_cast<HWND>(window.hwnd())) < ZRank(peer));

  // Hide and restore: the pin state and the topmost band both survive.
  window.Hide();
  PumpFor(30);
  window.Show();
  PumpFor(30);
  DHEPZ_CHECK(window.pinned());
  DHEPZ_CHECK(ZRank(static_cast<HWND>(window.hwnd())) < ZRank(peer));

  // Unpin: the peer, raised to the top, comes back in front.
  window.TogglePin();
  PumpFor(30);
  DHEPZ_CHECK_FALSE(window.pinned());
  SetWindowPos(peer, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  PumpFor(30);
  DHEPZ_CHECK(ZRank(peer) < ZRank(static_cast<HWND>(window.hwnd())));

  DestroyWindow(peer);
}

DHEPZ_TEST(AppWindow, PinTogglesDoNotGrowHandles) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance(), 320.0f, 240.0f));
  window.Show();
  PumpFor(30);

  const HANDLE process = GetCurrentProcess();
  const DWORD gdi_before = GetGuiResources(process, GR_GDIOBJECTS);
  const DWORD user_before = GetGuiResources(process, GR_USEROBJECTS);

  for (int i = 0; i < 50; ++i) {
    window.TogglePin();  // repaints every toggle: glyph state changed
  }
  PumpFor(60);

  const long long gdi_drift = static_cast<long long>(GetGuiResources(process, GR_GDIOBJECTS)) -
                              static_cast<long long>(gdi_before);
  const long long user_drift = static_cast<long long>(GetGuiResources(process, GR_USEROBJECTS)) -
                               static_cast<long long>(user_before);
  DHEPZ_CHECK(gdi_drift <= 2);
  DHEPZ_CHECK(user_drift <= 2);
}

DHEPZ_TEST(AppWindow, SettingsButtonAppearsOnlyWithHandler) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(TestInstance()));  // 960x640 content -> 1008x688
  window.Show();
  PumpFor(30);
  const HWND hwnd = static_cast<HWND>(window.hwnd());

  // Client geometry at 96 DPI: margin 24, content_right 984, button 46 px.
  // Order left-to-right: pin, [settings], close.
  bool opened = false;
  // No handler: two buttons, the pin occupies x 892..938; the future gear
  // slot (x 846..892) is plain caption — clicking it does nothing.
  SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(870, 40));
  DHEPZ_CHECK_FALSE(opened);
  DHEPZ_CHECK_FALSE(window.pinned());
  SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(910, 40));
  DHEPZ_CHECK(window.pinned());
  SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(910, 40));
  DHEPZ_CHECK_FALSE(window.pinned());

  window.set_settings_handler([&opened] { opened = true; });
  // The gear now takes x 892..938 and the pin shifts left to 846..892.
  SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(910, 40));
  DHEPZ_CHECK(opened);
  SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(870, 40));
  DHEPZ_CHECK(window.pinned());
}

#include "platform/settle_timer.h"

#include <windows.h>

#include <chrono>
#include <thread>

#include "framework/test_case.h"
#include "ui/app_window/app_window.h"

namespace {

platform::SettleTimer* g_settle = nullptr;
int g_timer_messages = 0;

LRESULT CALLBACK SettleTestProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_TIMER) {
    ++g_timer_messages;
    if (g_settle != nullptr && wparam == 777) {
      g_settle->OnTimer();
      return 0;
    }
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

HWND CreateSettleWindow() {
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = SettleTestProc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"dhepz.settle.test";
  RegisterClassW(&window_class);  // already-exists is fine
  return CreateWindowExW(0, window_class.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr,
                         nullptr, window_class.hInstance, nullptr);
}

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

bool PumpUntilSettled(int& settles, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    PumpFor(2);
    if (settles > 0) return true;
  }
  return settles > 0;
}

}  // namespace

DHEPZ_TEST(SettleTimer, FiresOnceAndIsOneShotByConstruction) {
  HWND window = CreateSettleWindow();
  DHEPZ_CHECK(window != nullptr);

  int settles = 0;
  platform::SettleTimer timer(window, 777, [&] { ++settles; });
  g_settle = &timer;
  g_timer_messages = 0;

  timer.Arm(30);
  DHEPZ_CHECK(timer.armed());
  DHEPZ_CHECK(PumpUntilSettled(settles, 2000));
  DHEPZ_CHECK_FALSE(timer.armed());  // killed first thing in the handler

  // Keep pumping: nothing more may fire. The timer no longer exists.
  PumpFor(300);
  DHEPZ_CHECK_EQ(settles, 1);
  DHEPZ_CHECK_EQ(g_timer_messages, 1);

  g_settle = nullptr;
  DestroyWindow(window);
}

DHEPZ_TEST(SettleTimer, RearmingResetsInsteadOfStacking) {
  HWND window = CreateSettleWindow();
  DHEPZ_CHECK(window != nullptr);

  int settles = 0;
  platform::SettleTimer timer(window, 777, [&] { ++settles; });
  g_settle = &timer;

  // A burst: 100 re-arms while the countdown is still running.
  for (int i = 0; i < 100; ++i) {
    timer.Arm(40);
    PumpFor(2);
  }
  DHEPZ_CHECK(PumpUntilSettled(settles, 2000));

  // Exactly one settle for the whole burst — timers never stacked.
  PumpFor(300);
  DHEPZ_CHECK_EQ(settles, 1);
  DHEPZ_CHECK_FALSE(timer.armed());

  g_settle = nullptr;
  DestroyWindow(window);
}

DHEPZ_TEST(SettleTimer, IdleAfterSettleHasNoTimerArmed) {
  HWND window = CreateSettleWindow();
  DHEPZ_CHECK(window != nullptr);

  int settles = 0;
  platform::SettleTimer timer(window, 777, [&] { ++settles; });
  g_settle = &timer;
  g_timer_messages = 0;

  timer.Arm(30);
  DHEPZ_CHECK(PumpUntilSettled(settles, 2000));

  // The audit: after activity has ceased, a long idle window delivers no
  // timer messages at all, and nothing is armed.
  const int before = g_timer_messages;
  PumpFor(400);
  DHEPZ_CHECK_EQ(g_timer_messages, before);
  DHEPZ_CHECK_FALSE(timer.armed());

  g_settle = nullptr;
  DestroyWindow(window);
}

DHEPZ_TEST(SettleTimer, DestructorKillsAnArmedTimer) {
  HWND window = CreateSettleWindow();
  DHEPZ_CHECK(window != nullptr);
  g_timer_messages = 0;

  {
    platform::SettleTimer timer(window, 777, [] {});
    timer.Arm(30);
    DHEPZ_CHECK(timer.armed());
  }  // destroyed while armed

  // Nothing may fire after the owner is gone.
  g_settle = nullptr;
  PumpFor(200);
  DHEPZ_CHECK_EQ(g_timer_messages, 0);
  DestroyWindow(window);
}

DHEPZ_TEST(SettleTimer, AppWindowResizeStormSettlesOnce) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(GetModuleHandleW(nullptr), 320.0f, 240.0f));
  window.Show();
  PumpFor(30);

  int settles = 0;
  window.set_settle_handler([&] { ++settles; });

  const HWND hwnd = static_cast<HWND>(window.hwnd());
  for (int i = 0; i < 100; ++i) {
    SendMessageW(hwnd, WM_SIZE, 0, MAKELPARAM(330 + (i % 5), 250 + (i % 3)));
  }
  DHEPZ_CHECK(PumpUntilSettled(settles, 2000));
  PumpFor(300);
  // One burst of 100 resizes settles exactly once.
  DHEPZ_CHECK_EQ(settles, 1);
}

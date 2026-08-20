#include "platform/signal_fanout.h"

#include <windows.h>

#include <chrono>
#include <thread>

#include "framework/test_case.h"
#include "ui/shell/app_window.h"

namespace {

platform::SignalFanout* g_fanout = nullptr;
unsigned int g_drain_message = 0;

LRESULT CALLBACK FanoutTestProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (g_fanout != nullptr) {
    if (message == g_drain_message) {
      g_fanout->DrainMessage();
      return 0;
    }
    if (message == WM_THEMECHANGED) {
      g_fanout->Raise(platform::OsSignal::Theme);
      return 0;
    }
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

HWND CreateFanoutWindow() {
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = FanoutTestProc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"dhepz.fanout.test";
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

}  // namespace

DHEPZ_TEST(SignalFanout, HundredBroadcastsProduceOneFanOut) {
  HWND window = CreateFanoutWindow();
  DHEPZ_CHECK(window != nullptr);
  g_drain_message = RegisterWindowMessageW(L"dhepz.fanout.test.drain");

  int drains = 0;
  std::uint32_t mask = 0;
  platform::SignalFanout fanout(window, g_drain_message,
                                [&](std::uint32_t signals) {
                                  ++drains;
                                  mask |= signals;
                                });
  g_fanout = &fanout;

  for (int i = 0; i < 100; ++i) {
    SendMessageW(window, WM_THEMECHANGED, 0, 0);
  }
  PumpFor(80);

  DHEPZ_CHECK_EQ(drains, 1);
  DHEPZ_CHECK((mask & static_cast<std::uint32_t>(platform::OsSignal::Theme)) != 0);
  g_fanout = nullptr;
  DestroyWindow(window);
}

DHEPZ_TEST(SignalFanout, AFailedPostStillAppliesTheSignal) {
  HWND window = CreateFanoutWindow();
  DHEPZ_CHECK(window != nullptr);
  DestroyWindow(window);  // a dead target: PostMessage will fail

  int drains = 0;
  std::uint32_t mask = 0;
  platform::SignalFanout fanout(window, 0x9999, [&](std::uint32_t signals) {
    ++drains;
    mask |= signals;
  });

  fanout.Raise(platform::OsSignal::Theme);
  // No pump ran: the drain happened synchronously inside Raise.
  DHEPZ_CHECK_EQ(drains, 1);
  DHEPZ_CHECK((mask & static_cast<std::uint32_t>(platform::OsSignal::Theme)) != 0);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(fanout.pending()),
                 static_cast<unsigned long long>(0));
}

DHEPZ_TEST(SignalFanout, ASignalRaisedMidDrainIsNotLost) {
  HWND window = CreateFanoutWindow();
  DHEPZ_CHECK(window != nullptr);
  const unsigned int drain_message = RegisterWindowMessageW(L"dhepz.fanout.test.drain2");

  int drains = 0;
  std::uint32_t first_mask = 0;
  std::uint32_t second_mask = 0;
  platform::SignalFanout* fanout_ptr = nullptr;
  platform::SignalFanout fanout(window, drain_message, [&](std::uint32_t signals) {
    ++drains;
    if (drains == 1) {
      first_mask = signals;
      // Arriving "mid-drain": the handler runs while the drain is in
      // flight. Raising here must not vanish.
      fanout_ptr->Raise(platform::OsSignal::Settings);
    } else {
      second_mask |= signals;
    }
  });
  fanout_ptr = &fanout;
  g_drain_message = drain_message;
  g_fanout = &fanout;

  SendMessageW(window, WM_THEMECHANGED, 0, 0);
  PumpFor(120);

  DHEPZ_CHECK_EQ(drains, 2);
  DHEPZ_CHECK((first_mask & static_cast<std::uint32_t>(platform::OsSignal::Theme)) != 0);
  DHEPZ_CHECK((second_mask & static_cast<std::uint32_t>(platform::OsSignal::Settings)) != 0);
  g_fanout = nullptr;
  DestroyWindow(window);
}

DHEPZ_TEST(SignalFanout, WithoutAWindowSignalsWaitForAttach) {
  int drains = 0;
  std::uint32_t mask = 0;
  platform::SignalFanout fanout(nullptr, 0x9999, [&](std::uint32_t signals) {
    ++drains;
    mask |= signals;
  });

  fanout.Raise(platform::OsSignal::Theme);
  fanout.Raise(platform::OsSignal::Display);
  DHEPZ_CHECK_EQ(drains, 0);  // recorded, not processed — zero cost
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(fanout.pending()),
                 static_cast<unsigned long long>(
                     static_cast<std::uint32_t>(platform::OsSignal::Theme) |
                     static_cast<std::uint32_t>(platform::OsSignal::Display)));

  HWND window = CreateFanoutWindow();
  DHEPZ_CHECK(window != nullptr);
  fanout.Attach(window);
  // Attach drains what was recorded: one fan-out carrying both signals.
  DHEPZ_CHECK_EQ(drains, 1);
  DHEPZ_CHECK((mask & static_cast<std::uint32_t>(platform::OsSignal::Theme)) != 0);
  DHEPZ_CHECK((mask & static_cast<std::uint32_t>(platform::OsSignal::Display)) != 0);
  DestroyWindow(window);
}

DHEPZ_TEST(SignalFanout, AppWindowCoalescesBroadcastBursts) {
  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(GetModuleHandleW(nullptr), 320.0f, 240.0f));

  int drains = 0;
  std::uint32_t mask = 0;
  window.set_signal_handler([&](std::uint32_t signals) {
    ++drains;
    mask |= signals;
  });

  const HWND hwnd = static_cast<HWND>(window.hwnd());
  SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
  SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
  SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
  SendMessageW(hwnd, WM_SYSCOLORCHANGE, 0, 0);
  SendMessageW(hwnd, WM_DISPLAYCHANGE, 0, 0);
  PumpFor(120);

  // One burst, one drain, all three signal kinds in the mask.
  DHEPZ_CHECK_EQ(drains, 1);
  DHEPZ_CHECK((mask & static_cast<std::uint32_t>(platform::OsSignal::Theme)) != 0);
  DHEPZ_CHECK((mask & static_cast<std::uint32_t>(platform::OsSignal::SystemColors)) != 0);
  DHEPZ_CHECK((mask & static_cast<std::uint32_t>(platform::OsSignal::Display)) != 0);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(window.last_os_signals()),
                 static_cast<unsigned long long>(mask));
}

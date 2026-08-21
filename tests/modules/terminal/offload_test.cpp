#include "modules/terminal/terminal_offload.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "framework/test_case.h"

namespace {

struct RigState {
  unsigned int completion_message = 0;
  unsigned int ping_message = 0;
  std::atomic<int> pings{0};
  std::chrono::steady_clock::time_point ping_received;
};

RigState* g_rig = nullptr;

LRESULT CALLBACK RigProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (g_rig != nullptr && message == g_rig->completion_message) {
    worker::Worker::Settle(lparam);
    return 0;
  }
  if (g_rig != nullptr && message == g_rig->ping_message) {
    g_rig->ping_received = std::chrono::steady_clock::now();
    ++g_rig->pings;
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

struct Rig {
  HWND hwnd = nullptr;
  RigState state;

  Rig() {
    g_rig = &state;
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = RigProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"dhepz.offload.test";
    RegisterClassW(&window_class);
    state.completion_message = RegisterWindowMessageW(L"dhepz.offload.test.completion");
    state.ping_message = RegisterWindowMessageW(L"dhepz.offload.test.ping");
    hwnd = CreateWindowExW(0, window_class.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr,
                           nullptr, window_class.hInstance, nullptr);
    DHEPZ_CHECK(hwnd != nullptr);
  }
  ~Rig() {
    if (hwnd != nullptr) DestroyWindow(hwnd);
    g_rig = nullptr;
  }
};

template <typename Pred>
void PumpUntil(Pred predicate, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (predicate()) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace

DHEPZ_TEST(TerminalOffload, CaptureRunsOffUiThreadAndDeliversOnIt) {
  Rig rig;
  terminal::TerminalOffload offload(rig.hwnd, rig.state.completion_message);

  std::atomic<bool> delivered{false};
  std::atomic<unsigned long> work_thread{0};
  const unsigned long main_thread = GetCurrentThreadId();

  // Custom Submit so the test can observe the worker thread id.
  offload.worker().Submit(
      [&work_thread](const std::atomic<bool>&) {
        work_thread = GetCurrentThreadId();
        auto result = std::make_shared<process::RunResult>();
        const core::Status status =
            process::RunCapture(L"cmd.exe /c echo offload-ok", L"", 10000, result.get());
        (void)status;
        return std::static_pointer_cast<void>(result);
      },
      [&](std::shared_ptr<void> cargo) {
        const auto& result = *std::static_pointer_cast<process::RunResult>(cargo);
        DHEPZ_CHECK(result.output.find(L"offload-ok") != std::wstring::npos);
        delivered = true;
      });

  PumpUntil([&] { return delivered.load(); }, 10000);
  DHEPZ_CHECK(delivered.load());
  DHEPZ_CHECK(work_thread.load() != 0);
  DHEPZ_CHECK(work_thread.load() != main_thread);
  offload.worker().Shutdown();
}

DHEPZ_TEST(TerminalOffload, UiStaysResponsiveWhileWorkerBlocks) {
  Rig rig;
  terminal::TerminalOffload offload(rig.hwnd, rig.state.completion_message);

  std::atomic<bool> delivered{false};
  offload.RunCaptureAsync(L"cmd.exe /c timeout /t 2 /nobreak >nul",
                          [&](const process::RunResult&) { delivered = true; });

  // While the worker blocks for ~2 s, an unrelated message must round-trip
  // fast — the UI thread never blocks on the child.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const auto sent = std::chrono::steady_clock::now();
  PostMessageW(rig.hwnd, rig.state.ping_message, 0, 0);
  PumpUntil([&] { return rig.state.pings.load() > 0; }, 2000);
  const double ms = std::chrono::duration<double, std::milli>(
                        rig.state.ping_received - sent)
                        .count();
  DHEPZ_CHECK(rig.state.pings.load() > 0);
  DHEPZ_CHECK(ms < 100.0);

  PumpUntil([&] { return delivered.load(); }, 10000);
  DHEPZ_CHECK(delivered.load());
  offload.worker().Shutdown();
}

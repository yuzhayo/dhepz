#include "platform/tray_process.h"

#include <windows.h>

#include <chrono>
#include <thread>

#include "framework/test_case.h"

namespace {

void PumpFor(int milliseconds) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < deadline) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace

// The criterion that used to cost the old build a real bug: the
// infrastructure window must be a real top-level WS_POPUP with
// WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, and never an HWND_MESSAGE window —
// message-only windows do not receive the TaskbarCreated broadcast, so the
// tray icon would be lost forever after an Explorer restart.
DHEPZ_TEST(TrayProcess, WindowIsATopLevelPopupNotMessageOnly) {
  tray::TrayProcess process;
  DHEPZ_CHECK(process.Start(GetModuleHandleW(nullptr)) == tray::StartResult::Ok);

  HWND hwnd = static_cast<HWND>(process.window());
  DHEPZ_CHECK(IsWindow(hwnd) == TRUE);
  // Top-level means no parent; a message-only window would report
  // HWND_MESSAGE here instead of nullptr.
  DHEPZ_CHECK(GetParent(hwnd) == nullptr);

  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  const LONG_PTR exstyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  DHEPZ_CHECK((style & WS_POPUP) == WS_POPUP);
  DHEPZ_CHECK((style & WS_CHILD) == 0);
  DHEPZ_CHECK((exstyle & WS_EX_TOOLWINDOW) == WS_EX_TOOLWINDOW);
  DHEPZ_CHECK((exstyle & WS_EX_NOACTIVATE) == WS_EX_NOACTIVATE);

  process.Shutdown();
  DHEPZ_CHECK(IsWindow(hwnd) == FALSE);
  // Shutdown is idempotent.
  process.Shutdown();
}

DHEPZ_TEST(TrayProcess, TrayInstallIsHonestAboutItsEnvironment) {
  tray::TrayProcess process;
  DHEPZ_CHECK(process.Start(GetModuleHandleW(nullptr)) == tray::StartResult::Ok);

  // Whether the shell accepts the icon depends on the environment (a
  // headless CI runner has no tray), so the contract under test is that the
  // call reports truthfully and never crashes — not a fixed outcome.
  const bool installed = process.InstallTray();
  DHEPZ_CHECK(process.tray_installed() == installed);
  const bool again = process.InstallTray();
  DHEPZ_CHECK(again == installed);

  process.Shutdown();
  DHEPZ_CHECK_FALSE(process.tray_installed());
}

DHEPZ_TEST(TrayProcess, SecondLaunchRequestsAnotherWindowFromTheOwner) {
  tray::TrayProcess owner;
  DHEPZ_CHECK(owner.Start(GetModuleHandleW(nullptr)) == tray::StartResult::Ok);
  bool launch_requested = false;
  owner.set_launch_handler([&launch_requested] { launch_requested = true; });

  tray::TrayProcess second;
  DHEPZ_CHECK(second.Start(GetModuleHandleW(nullptr)) ==
              tray::StartResult::ExistingOwnerNotified);
  DHEPZ_CHECK(second.window() == nullptr);
  PumpFor(20);
  DHEPZ_CHECK(launch_requested);
}

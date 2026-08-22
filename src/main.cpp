#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "app_version.h"
#include "platform/performance_trace.h"
#include "platform/tray_process.h"
#include "ui/shell/app_window/app_window.h"

// P1-P2 composition root: one tray-resident owner process and one or more
// empty AppWindow chrome containers. UI routing and feature logic arrive in
// later phases. The single UI thread blocks in GetMessageW while idle.
//
// wWinMain rather than main: SubSystem is Windows, so there is no console
// window to flash on launch. That matters for the ~400 ms cold-start budget
// and for resident tray activation.
namespace {

// Distinct numeric exit codes for every bootstrap failure, each with a
// MessageBox — no silent exits. The codes start at 10 so they cannot be
// confused with a normal 0/1 result.
[[noreturn]] void BootstrapFailed(unsigned int code, const wchar_t* what) {
  MessageBoxW(nullptr, what, L"dhepz", MB_OK | MB_ICONERROR);
  ExitProcess(code);
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance,
                      _In_opt_ HINSTANCE previous,
                      _In_ LPWSTR command_line,
                      _In_ int show_command) {
  UNREFERENCED_PARAMETER(previous);
  UNREFERENCED_PARAMETER(command_line);
  UNREFERENCED_PARAMETER(show_command);

  static_assert(dhepz::version::kMajor >= 0,
                "version.props must reach the compiler as defines");

  // The QPC captured on the first line is the ProcessEntry milestone. Later
  // phases add config-resolution milestones; P2 goes directly from bootstrap
  // to creating and showing an empty chrome container.
  const std::int64_t process_entry_qpc = trace::CurrentQpc();
  trace::PerformanceTraceSession session(process_entry_qpc);

  tray::TrayProcess tray_process;
  switch (tray_process.Start(instance)) {
    case tray::StartResult::Ok:
      break;
    case tray::StartResult::ExistingOwnerNotified:
      return 0;
    case tray::StartResult::SingleInstanceFailed:
      BootstrapFailed(10, L"The single-instance owner could not be established.");
    case tray::StartResult::ExistingOwnerNotificationFailed:
      BootstrapFailed(11, L"The existing application process could not create a window.");
    case tray::StartResult::WindowClassFailed:
      BootstrapFailed(12, L"The infrastructure window class could not be registered.");
    case tray::StartResult::WindowCreateFailed:
      BootstrapFailed(13, L"The infrastructure window could not be created.");
    case tray::StartResult::TaskbarMessageFailed:
      BootstrapFailed(14, L"The TaskbarCreated message could not be registered.");
  }

  // Non-fatal by design: a headless session has no shell to host the icon,
  // and the process must still run (CI, automated runs).
  tray_process.InstallTray();

  // One resident owner PID hosts every P2 window. A secondary EXE invocation
  // asks this owner to create a window, then exits without a tray of its own.
  std::vector<std::unique_ptr<shell::AppWindow>> windows;
  const auto create_window = [&]() -> bool {
    std::erase_if(windows, [](const std::unique_ptr<shell::AppWindow>& item) {
      return !item->alive();
    });
    auto item = std::make_unique<shell::AppWindow>();
    if (!item->Create(instance, 400.0f, 360.0f)) return false;
    // P2 exposes the chrome seam for visual review. P3 replaces this inert
    // callback with Settings screen navigation.
    item->set_settings_handler([] {});
    shell::AppWindow* const window = item.get();
    windows.push_back(std::move(item));
    window->Show();
    return true;
  };

  tray_process.set_launch_handler([&] {
    if (!create_window()) {
      MessageBoxW(nullptr, L"The application window could not be created.", L"dhepz",
                  MB_OK | MB_ICONERROR);
    }
  });
  if (!create_window()) {
    tray_process.Shutdown();
    BootstrapFailed(15, L"The application window could not be created.");
  }

  const int result = tray_process.Run();
  windows.clear();
  tray_process.Shutdown();
  return result;
}

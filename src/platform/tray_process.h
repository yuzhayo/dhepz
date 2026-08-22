// The tray-resident presence: one hidden infrastructure window and one tray
// icon, nothing else. This process is the baseline subject for G1 — its
// idle cost is the floor everything else is measured against — so it keeps
// exactly one thread, no timers, and a message loop that blocks.
//
// Two details here are load-bearing and easy to get wrong (both cost the
// old build real bugs):
//
//   - The window is a real top-level WS_POPUP with
//     WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, NOT an HWND_MESSAGE window.
//     Message-only windows never receive the TaskbarCreated broadcast, so
//     after an Explorer restart the tray icon would be gone forever.
//   - Shell_NotifyIconW(NIM_SETVERSION) is called AFTER the add. That call
//     is what enables the modern NOTIFYICON_VERSION_4 callback layout
//     (event in LOWORD(lParam)) and the NIN_* semantics the handler relies
//     on.
//
// Like all of platform/, this layer keeps windows.h out of the header.
#pragma once

#include <functional>

namespace trace {
class PerformanceTraceSession;
}

namespace tray {

// Distinct exit codes so a failed bootstrap says which step failed instead
// of dying silently (G5). Success is 0.
enum class StartResult {
  Ok,
  WindowClassFailed,
  WindowCreateFailed,
  TaskbarMessageFailed,
};

// Owns the infrastructure window and the tray icon for the life of the
// process. The trace session is passed in so the shutdown order stays
// explicit: the tray icon is removed and the window destroyed before the
// session unregisters the provider.
class TrayProcess final {
 public:
  TrayProcess() noexcept;
  ~TrayProcess();

  TrayProcess(const TrayProcess&) = delete;
  TrayProcess& operator=(const TrayProcess&) = delete;

  // Creates the infrastructure window and registers TaskbarCreated. Does
  // not touch the tray yet, so a headless environment can still create the
  // process; InstallTray is a separate, non-fatal step.
  StartResult Start(void* instance) noexcept;

  // Adds the tray icon and sets NOTIFYICON_VERSION_4. Returns false when the
  // shell refuses (a headless session): the process stays up without a tray.
  bool InstallTray() noexcept;

  // The shell owns what activation means. At P2 this only shows the empty
  // parent window; feature routing does not belong in the tray process.
  void set_activate_handler(std::function<void()> handler);

  // Blocks in GetMessageW until Exit. No timers, no polling, no idle work.
  int Run() noexcept;

  // Removes the tray icon and destroys the window. Safe to call twice.
  void Shutdown() noexcept;

  // For verification only: the window must be a real top-level popup.
  void* window() const noexcept { return window_; }
  bool tray_installed() const noexcept { return tray_icon_added_; }

 private:
  // Spelled without windows.h types; on x64 this is layout-identical to
  // LRESULT CALLBACK WNDPROC(HWND, UINT, WPARAM, LPARAM).
  static long long __stdcall WindowProcedure(void* window, unsigned int message,
                                             unsigned long long wparam, long long lparam);
  long long HandleMessage(unsigned int message, unsigned long long wparam,
                          long long lparam);
  void HandleTrayCallback(unsigned long long wparam, long long lparam);
  void ShowMenu();
  void AddTrayIcon() noexcept;

  void* instance_ = nullptr;
  void* window_ = nullptr;
  unsigned int taskbar_created_message_ = 0;
  bool tray_icon_added_ = false;
  std::function<void()> activate_handler_;
};

}  // namespace tray

#include "application/production_application.h"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <thread>

#include "framework/test_case.h"
#include "modules/gate/app_gate.h"
#include "modules/registry/module_registry.h"
#include "modules/terminal/terminal_module.h"
#include "ui/presenter/screen_presenter.h"
#include "ui/shell/app_window.h"

namespace {
class ScopedTerminalRegistration final {
 public:
  explicit ScopedTerminalRegistration(modules::ModuleFactory factory) {
    modules::ResetRegistryForTests();
    modules::RegisterModule(L"terminal", factory);
  }
  ~ScopedTerminalRegistration() { modules::ResetRegistryForTests(); }
};

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

DHEPZ_TEST(ProductionApplication, NormalLaunchComposesAndShowsEmbeddedJsonUi) {
  ScopedTerminalRegistration registration(&terminal::MakeTerminalForTests);
  application::ProductionApplication app;
  const core::Status start = app.Start(GetModuleHandleW(nullptr), false);
  DHEPZ_CHECK_EQ(start.Message(), std::wstring());
  DHEPZ_CHECK(app.gate() != nullptr);
  DHEPZ_CHECK(app.gate()->document() != nullptr);
  DHEPZ_CHECK(app.presenter() != nullptr);
  DHEPZ_CHECK_EQ(app.presenter()->current_route(), std::wstring(L"terminal"));
  DHEPZ_CHECK(app.window() != nullptr);
  DHEPZ_CHECK(app.window()->visible());
  DHEPZ_CHECK(app.window()->backend()->buffer_width() > 0);
  DHEPZ_CHECK(app.startup_trace_complete());
  app.Shutdown();
}

DHEPZ_TEST(ProductionApplication, TrayOnlyDefersUiUntilTrayActivation) {
  ScopedTerminalRegistration registration(&terminal::MakeTerminalForTests);
  application::ProductionApplication app;
  const core::Status start = app.Start(GetModuleHandleW(nullptr), true);
  DHEPZ_CHECK_EQ(start.Message(), std::wstring());
  DHEPZ_CHECK(app.window() == nullptr);
  DHEPZ_CHECK(app.presenter() == nullptr);
  DHEPZ_CHECK(app.gate() == nullptr);
  DHEPZ_CHECK_FALSE(app.startup_trace_complete());

  SendMessageW(static_cast<HWND>(app.tray_window()), WM_APP + 1, 0,
               MAKELPARAM(NIN_SELECT, 1));
  PumpFor(20);
  DHEPZ_CHECK(app.window() != nullptr);
  DHEPZ_CHECK(app.window()->visible());
  DHEPZ_CHECK(app.presenter() != nullptr);
  DHEPZ_CHECK(app.gate() != nullptr);
  app.Shutdown();
}

DHEPZ_TEST(ProductionApplication, CloseReleasesAndNextShowRebuildsCompleteFrame) {
  ScopedTerminalRegistration registration(&terminal::MakeTerminalForTests);
  application::ProductionApplication app;
  const core::Status start = app.Start(GetModuleHandleW(nullptr), false);
  DHEPZ_CHECK_EQ(start.Message(), std::wstring());
  shell::AppWindow* window = app.window();
  DHEPZ_CHECK(window != nullptr);

  SendMessageW(static_cast<HWND>(window->hwnd()), WM_CLOSE, 0, 0);
  DHEPZ_CHECK_FALSE(window->visible());
  DHEPZ_CHECK_EQ(window->backend()->buffer_width(), 0);

  DHEPZ_CHECK(app.ShowMainWindow().ok());
  DHEPZ_CHECK(window->visible());
  DHEPZ_CHECK(window->backend()->buffer_width() > 0);
  const int x = window->backend()->buffer_width() / 2;
  const int y = window->backend()->buffer_height() / 2;
  DHEPZ_CHECK_EQ(
      static_cast<unsigned long long>(window->backend()->PixelAt(x, y) >> 24),
      0xFFull);
  app.Shutdown();
}

DHEPZ_TEST(ProductionApplication, TerminalJsonActionReachesGateStatusPath) {
  ScopedTerminalRegistration registration(&terminal::MakeTerminalForTests);
  application::ProductionApplication app;
  const core::Status start = app.Start(GetModuleHandleW(nullptr), false);
  DHEPZ_CHECK_EQ(start.Message(), std::wstring());
  DHEPZ_CHECK(app.gate()->Mounted(L"terminal"));
  DHEPZ_CHECK(app.gate()->document()->FindRoute(L"terminal") != nullptr);
  DHEPZ_CHECK_EQ(app.presenter()->current_route(), std::wstring(L"terminal"));
  const json::Value* launch_enabled =
      app.presenter()->ViewStateValue(L"terminal", L"launch_enabled");
  DHEPZ_CHECK(launch_enabled != nullptr);
  DHEPZ_CHECK(launch_enabled->AsBool());

  app.presenter()->SwitchRoute(L"terminal");
  app.window()->Repaint();
  json::Value invalid_state = json::Value::Object();
  invalid_state.Set(L"working_folder", json::Value::String(L""));
  invalid_state.Set(L"launch_enabled", json::Value::Bool(true));
  DHEPZ_CHECK(
      app.presenter()->ApplyStatePatch(L"terminal", invalid_state).ok());
  render::Rect button{};
  DHEPZ_CHECK(app.presenter()->InteractiveBounds(L"launch-powershell", &button));
  DHEPZ_CHECK(button.width > 0.0f);
  DHEPZ_CHECK(button.height > 0.0f);
  DHEPZ_CHECK(app.presenter()->HitTestContent(
      button.x + button.width / 2.0f, button.y + button.height / 2.0f));
  DHEPZ_CHECK(app.presenter()->HandleClick(
      button.x + button.width / 2.0f,
      button.y + button.height / 2.0f));
  DHEPZ_CHECK_FALSE(app.presenter()->last_action_status().ok());
  DHEPZ_CHECK_CONTAINS(app.presenter()->last_action_status().Message(),
                       std::wstring(L"working_folder"));
  DHEPZ_CHECK_EQ(app.gate()->Diagnostics().statuses.size(),
                 static_cast<std::size_t>(1));
  DHEPZ_CHECK_FALSE(app.gate()->Diagnostics().statuses[0].ok);
  DHEPZ_CHECK(app.window()->visible());
  app.Shutdown();
}

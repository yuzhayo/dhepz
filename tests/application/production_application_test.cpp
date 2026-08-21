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
class FakeTerminalModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"terminal"; }
  std::wstring_view TabLabel() const override { return L"Terminal"; }
  int Order() const override { return 10; }
  bool ShowInTabs() const override { return false; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override {
    return {L"terminal:launch", L"terminal:browse-folder",
            L"terminal:refresh-wsl"};
  }
  std::vector<std::wstring> DeclaredBindings() const override {
    return {L"working_folder", L"recent_folders", L"wsl_distros",
            L"wsl_distro", L"venv_enabled", L"busy", L"status",
            L"launch_enabled"};
  }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }
  core::Status Bind(modules::ModuleHost& host) override {
    json::Value patch = json::Value::Object();
    patch.Set(L"launch_enabled", json::Value::Bool(true));
    return host.PublishStatePatch(patch);
  }
  core::Status Handle(std::wstring_view action, const json::Value&,
                      json::Value*) override {
    if (action != L"terminal:launch") {
      return core::Err(core::ErrorCode::NotFound, L"unexpected action");
    }
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"fake terminal dispatch observed");
  }
  void Release() override {}
};

std::unique_ptr<modules::ModuleDescriptor> MakeFakeTerminal() {
  return std::make_unique<FakeTerminalModule>();
}

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
  ScopedTerminalRegistration registration(&MakeFakeTerminal);
  application::ProductionApplication app;
  const core::Status start = app.Start(GetModuleHandleW(nullptr), false);
  DHEPZ_CHECK_EQ(start.Message(), std::wstring());

  app.presenter()->SwitchRoute(L"terminal");
  app.window()->Repaint();
  render::Rect button{};
  DHEPZ_CHECK(app.presenter()->InteractiveBounds(L"launch-powershell", &button));
  DHEPZ_CHECK(app.presenter()->HandleClick(
      button.x + button.width / 2.0f,
      button.y + button.height / 2.0f));
  DHEPZ_CHECK_FALSE(app.presenter()->last_action_status().ok());
  DHEPZ_CHECK_CONTAINS(app.presenter()->last_action_status().Message(),
                       std::wstring(L"dispatch observed"));
  DHEPZ_CHECK_EQ(app.gate()->Diagnostics().statuses.size(),
                 static_cast<std::size_t>(1));
  DHEPZ_CHECK_FALSE(app.gate()->Diagnostics().statuses[0].ok);
  DHEPZ_CHECK(app.window()->visible());
  app.Shutdown();
}

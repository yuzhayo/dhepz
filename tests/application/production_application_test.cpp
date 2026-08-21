#include "application/production_application.h"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <thread>

#include "framework/test_case.h"
#include "modules/gate/app_gate.h"
#include "ui/presenter/screen_presenter.h"
#include "ui/shell/app_window.h"

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

DHEPZ_TEST(ProductionApplication, NormalLaunchComposesAndShowsEmbeddedJsonUi) {
  application::ProductionApplication app;
  const core::Status start = app.Start(GetModuleHandleW(nullptr), false);
  DHEPZ_CHECK_EQ(start.Message(), std::wstring());
  DHEPZ_CHECK(app.gate() != nullptr);
  DHEPZ_CHECK(app.gate()->document() != nullptr);
  DHEPZ_CHECK(app.presenter() != nullptr);
  DHEPZ_CHECK_EQ(app.presenter()->current_route(), std::wstring(L"home"));
  DHEPZ_CHECK(app.window() != nullptr);
  DHEPZ_CHECK(app.window()->visible());
  DHEPZ_CHECK(app.window()->backend()->buffer_width() > 0);
  DHEPZ_CHECK(app.startup_trace_complete());
  app.Shutdown();
}

DHEPZ_TEST(ProductionApplication, TrayOnlyDefersUiUntilTrayActivation) {
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

#include "modules/gate/app_gate.h"

#include <windows.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "framework/test_case.h"
#include "modules/registry/module_registry.h"
#include "platform/worker.h"

namespace {

class ResourceAlpha final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"resource-alpha"; }
  std::wstring_view TabLabel() const override { return L"resource-alpha"; }
  int Order() const override { return 100; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return {}; }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }
  core::Status Bind(modules::ModuleHost&) override { return core::Ok(); }
  core::Status Handle(std::wstring_view, const json::Value&,
                      json::Value*) override {
    return core::Ok();
  }
  void Release() override {}
};

std::unique_ptr<modules::ModuleDescriptor> MakeResourceAlpha() {
  return std::make_unique<ResourceAlpha>();
}

struct StartupRigState {
  unsigned int completion_message = 0;
};
StartupRigState* g_startup_rig = nullptr;

LRESULT CALLBACK StartupRigProc(HWND window, UINT message, WPARAM wparam,
                                LPARAM lparam) {
  if (g_startup_rig != nullptr &&
      message == g_startup_rig->completion_message) {
    worker::Worker::Settle(lparam);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

struct StartupRig {
  HWND window = nullptr;
  StartupRigState state;
  StartupRig() {
    g_startup_rig = &state;
    WNDCLASSW cls{};
    cls.lpfnWndProc = StartupRigProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"dhepz.app-gate-startup.test";
    RegisterClassW(&cls);
    state.completion_message =
        RegisterWindowMessageW(L"dhepz.app-gate-startup.completion");
    window = CreateWindowExW(0, cls.lpszClassName, L"", WS_POPUP, 0, 0, 16,
                             16, nullptr, nullptr, cls.hInstance, nullptr);
    DHEPZ_CHECK(window != nullptr);
  }
  ~StartupRig() {
    if (window != nullptr) DestroyWindow(window);
    g_startup_rig = nullptr;
  }
};

template <typename Predicate>
void PumpUntil(Predicate predicate, int timeout_ms = 2000) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (predicate()) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace

DHEPZ_TEST(AppGateStartup, ResourceStartQueuesOverrideIoAndFallsBackHidden) {
  StartupRig rig;
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"resource-alpha", &MakeResourceAlpha);
  modules::AppGate gate;
  DHEPZ_CHECK(gate.ConfigureHostOperations(
                      rig.window, rig.state.completion_message, {})
                  .ok());
  const std::wstring missing =
      L"Z:\\dhepz-app-gate-test\\missing-override.json";
  DHEPZ_CHECK(gate.StartFromResource(L"APP_GATE_TEST_UI", missing).ok());
  DHEPZ_CHECK(gate.start_pending());
  DHEPZ_CHECK(gate.document() == nullptr);

  PumpUntil([&] { return !gate.start_pending(); });
  DHEPZ_CHECK(!gate.start_pending());
  DHEPZ_CHECK(gate.start_status().ok());
  DHEPZ_CHECK(gate.Mounted(L"resource-alpha"));
  DHEPZ_CHECK(gate.document()->FindRoute(L"resource-alpha") != nullptr);
  DHEPZ_CHECK_EQ(gate.Rejects().size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK_EQ(gate.Rejects()[0].file, missing);
  modules::ResetRegistryForTests();
}

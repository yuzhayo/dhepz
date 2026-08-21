#include "modules/gate/app_gate.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "framework/test_case.h"
#include "modules/registry/module_registry.h"
#include "platform/worker.h"

namespace {

enum class FaultMode {
  None,
  BindStatus,
  BindThrow,
  InvalidArgument,
  IoError,
  Cancelled,
  Internal,
  HandleThrow,
  StartAsync,
};

FaultMode g_fault_mode = FaultMode::None;
int g_bind_count = 0;
int g_release_count = 0;
std::atomic<bool> g_async_completion{false};

class FaultModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"faulty"; }
  std::wstring_view TabLabel() const override { return L"Faulty"; }
  int Order() const override { return 50; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override {
    return {L"faulty:run"};
  }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }
  core::Status Bind(modules::ModuleHost& host) override {
    ++g_bind_count;
    if (g_fault_mode == FaultMode::BindThrow) throw std::runtime_error("bind");
    if (g_fault_mode == FaultMode::BindStatus) {
      return core::Err(core::ErrorCode::IoError, L"bind failed");
    }
    if (g_fault_mode == FaultMode::StartAsync) {
      modules::ProcessRequest request;
      request.operation = modules::ProcessOperation::Capture;
      request.executable = L"cmd.exe";
      request.arguments = {L"/d", L"/s", L"/c",
                           L"ping -n 3 127.0.0.1 >nul"};
      request.timeout_ms = 5000;
      modules::AsyncRequestToken token;
      return host.StartProcess(
          request,
          [](const modules::HostOperationCompletion&) {
            g_async_completion = true;
          },
          &token);
    }
    return core::Ok();
  }
  core::Status Handle(std::wstring_view, const json::Value&,
                      json::Value*) override {
    if (g_fault_mode == FaultMode::HandleThrow) {
      throw std::runtime_error("handle");
    }
    if (g_fault_mode == FaultMode::InvalidArgument) {
      return core::Err(core::ErrorCode::InvalidArgument, L"bad input");
    }
    if (g_fault_mode == FaultMode::IoError) {
      return core::Err(core::ErrorCode::IoError, L"expected IO failure");
    }
    if (g_fault_mode == FaultMode::Cancelled) {
      return core::Err(core::ErrorCode::Cancelled, L"cancelled");
    }
    if (g_fault_mode == FaultMode::Internal) {
      return core::Err(core::ErrorCode::Internal, L"invariant broken");
    }
    return core::Ok();
  }
  void Release() override { ++g_release_count; }
};

class HealthyModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"healthy"; }
  std::wstring_view TabLabel() const override { return L"Healthy"; }
  int Order() const override { return 60; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override {
    return {L"healthy:run"};
  }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }
  core::Status Bind(modules::ModuleHost&) override { return core::Ok(); }
  core::Status Handle(std::wstring_view, const json::Value&,
                      json::Value*) override {
    return core::Ok();
  }
  void Release() override {}
};

std::unique_ptr<modules::ModuleDescriptor> MakeFault() {
  return std::make_unique<FaultModule>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeHealthy() {
  return std::make_unique<HealthyModule>();
}

std::wstring Embedded() {
  return LR"({
    "core": {
      "schema": "dhepz.ui.core", "version": 1,
      "tokens": { "dark": { "text": "#ffffff" }, "light": { "text": "#000000" } },
      "allows_children": ["screen"],
      "components": { "screen": { "properties": {
        "route_id": { "kind": "string", "required": true },
        "module_id": { "kind": "string" },
        "tab_label": { "kind": "string" },
        "show_in_tabs": { "kind": "bool" }
      } } }
    },
    "components": [
      { "type": "screen", "route_id": "faulty", "module_id": "faulty",
        "tab_label": "Faulty" },
      { "type": "screen", "route_id": "healthy", "module_id": "healthy",
        "tab_label": "Healthy" }
    ],
    "modules": [
      { "moduleId": "faulty", "tabLabel": "Faulty", "order": 50,
        "actions": ["faulty:run"] },
      { "moduleId": "healthy", "tabLabel": "Healthy", "order": 60,
        "actions": ["healthy:run"] }
    ]
  })";
}

void Reset(FaultMode mode) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"faulty", &MakeFault);
  modules::RegisterModule(L"healthy", &MakeHealthy);
  g_fault_mode = mode;
  g_bind_count = 0;
  g_release_count = 0;
  g_async_completion = false;
}

core::Status DispatchFault(modules::AppGate* gate) {
  json::Value payload;
  json::Value patch;
  return gate->Dispatch(L"faulty:run", payload, &patch);
}

unsigned int g_completion_message = 0;
LRESULT CALLBACK QuarantineRigProc(HWND window, UINT message, WPARAM wparam,
                                   LPARAM lparam) {
  if (message == g_completion_message) {
    worker::Worker::Settle(lparam);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

struct Rig {
  HWND window = nullptr;
  Rig() {
    WNDCLASSW cls{};
    cls.lpfnWndProc = QuarantineRigProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"dhepz.runtime-quarantine.test";
    RegisterClassW(&cls);
    g_completion_message =
        RegisterWindowMessageW(L"dhepz.runtime-quarantine.completion");
    window = CreateWindowExW(0, cls.lpszClassName, L"", WS_POPUP, 0, 0, 8, 8,
                             nullptr, nullptr, cls.hInstance, nullptr);
    DHEPZ_CHECK(window != nullptr);
  }
  ~Rig() {
    if (window != nullptr) DestroyWindow(window);
  }
};

void PumpFor(int milliseconds) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < deadline) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      DispatchMessageW(&message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace

DHEPZ_TEST(RuntimeQuarantine, OperationalStatusesDoNotDisableModule) {
  for (const FaultMode mode : {FaultMode::InvalidArgument, FaultMode::IoError,
                               FaultMode::Cancelled}) {
    Reset(mode);
    modules::AppGate gate;
    DHEPZ_CHECK(gate.StartWithEmbedded(Embedded()).ok());
    DHEPZ_CHECK(!DispatchFault(&gate).ok());
    DHEPZ_CHECK(gate.Mounted(L"faulty"));
    DHEPZ_CHECK(gate.document()->FindRoute(L"faulty") != nullptr);
    DHEPZ_CHECK(gate.Diagnostics().runtime_faults.empty());
    bool saw_operational_status = false;
    for (const modules::ModuleStatusEntry& status : gate.Diagnostics().statuses) {
      if (status.module_id == L"faulty" && !status.ok) {
        saw_operational_status = true;
      }
    }
    DHEPZ_CHECK(saw_operational_status);
    json::Value payload;
    json::Value patch;
    DHEPZ_CHECK(gate.Dispatch(L"healthy:run", payload, &patch).ok());
  }
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(RuntimeQuarantine, BindFailureAndExceptionQuarantineOnce) {
  for (const FaultMode mode : {FaultMode::BindStatus, FaultMode::BindThrow}) {
    Reset(mode);
    modules::AppGate gate;
    DHEPZ_CHECK(gate.StartWithEmbedded(Embedded()).ok());
    DHEPZ_CHECK(!gate.Activate(L"faulty").ok());
    DHEPZ_CHECK(!gate.Mounted(L"faulty"));
    DHEPZ_CHECK(gate.document()->FindRoute(L"faulty") == nullptr);
    DHEPZ_CHECK_EQ(g_release_count, 1);
    const modules::DiagnosticsReadModel diagnostics = gate.Diagnostics();
    DHEPZ_CHECK_EQ(diagnostics.runtime_faults.size(),
                   static_cast<std::size_t>(1));
    DHEPZ_CHECK(diagnostics.runtime_faults[0].stage ==
                modules::DiagnosticStage::Bind);
    DHEPZ_CHECK_EQ(diagnostics.runtime_faults[0].file,
                   std::wstring(L"runtime"));
    DHEPZ_CHECK(diagnostics.runtime_faults[0].line > 0);
    DHEPZ_CHECK(!DispatchFault(&gate).ok());
    DHEPZ_CHECK_EQ(g_release_count, 1);
  }
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(RuntimeQuarantine, HandleExceptionAndInternalStatusQuarantine) {
  for (const FaultMode mode : {FaultMode::HandleThrow, FaultMode::Internal}) {
    Reset(mode);
    modules::AppGate gate;
    DHEPZ_CHECK(gate.StartWithEmbedded(Embedded()).ok());
    DHEPZ_CHECK(!DispatchFault(&gate).ok());
    DHEPZ_CHECK(!gate.Mounted(L"faulty"));
    DHEPZ_CHECK(gate.document()->FindRoute(L"faulty") == nullptr);
    DHEPZ_CHECK_EQ(g_release_count, 1);
    DHEPZ_CHECK_EQ(gate.Diagnostics().runtime_faults.size(),
                   static_cast<std::size_t>(1));
    json::Value payload;
    json::Value patch;
    DHEPZ_CHECK(gate.Dispatch(L"healthy:run", payload, &patch).ok());
  }
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(ModuleLifecycle, WindowReleaseAndShutdownAreIdempotent) {
  Reset(FaultMode::None);
  modules::AppGate gate;
  DHEPZ_CHECK(gate.StartWithEmbedded(Embedded()).ok());
  DHEPZ_CHECK(gate.Activate(L"faulty").ok());
  DHEPZ_CHECK_EQ(g_bind_count, 1);
  const ui::config::ResolvedUiDocument* document_before_release = gate.document();
  const std::uint64_t generation_before_release = gate.document_generation();
  gate.ReleaseWindowModules();
  gate.ReleaseWindowModules();
  DHEPZ_CHECK_EQ(g_release_count, 1);
  DHEPZ_CHECK(gate.document() == document_before_release);
  DHEPZ_CHECK_EQ(gate.document_generation(), generation_before_release);

  DHEPZ_CHECK(gate.Activate(L"faulty").ok());
  DHEPZ_CHECK_EQ(g_bind_count, 2);
  gate.Shutdown();
  gate.Shutdown();
  DHEPZ_CHECK_EQ(g_release_count, 2);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(ModuleLifecycle, WindowReleaseCancelsOutstandingCompletion) {
  Rig rig;
  Reset(FaultMode::StartAsync);
  modules::AppGate gate;
  DHEPZ_CHECK(
      gate.ConfigureHostOperations(rig.window, g_completion_message, {}).ok());
  DHEPZ_CHECK(gate.StartWithEmbedded(Embedded()).ok());
  DHEPZ_CHECK(gate.Activate(L"faulty").ok());
  gate.ReleaseWindowModules();
  PumpFor(250);
  DHEPZ_CHECK(!g_async_completion.load());
  DHEPZ_CHECK_EQ(g_release_count, 1);
  modules::ResetRegistryForTests();
}

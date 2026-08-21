#include "modules/gate/host_operation_dispatcher.h"

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "framework/test_case.h"
#include "modules/gate/app_gate.h"
#include "modules/registry/module_registry.h"
#include "platform/worker.h"

namespace {

struct RigState {
  unsigned int completion_message = 0;
  unsigned int ping_message = 0;
  std::atomic<int> pings{0};
};

RigState* g_state = nullptr;

LRESULT CALLBACK HostRigProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (g_state != nullptr && message == g_state->completion_message) {
    worker::Worker::Settle(lparam);
    return 0;
  }
  if (g_state != nullptr && message == g_state->ping_message) {
    ++g_state->pings;
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

struct Rig {
  HWND window = nullptr;
  RigState state;

  Rig() {
    g_state = &state;
    WNDCLASSW cls{};
    cls.lpfnWndProc = HostRigProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"dhepz.host-operation.test";
    RegisterClassW(&cls);
    state.completion_message = RegisterWindowMessageW(L"dhepz.host-operation.completion");
    state.ping_message = RegisterWindowMessageW(L"dhepz.host-operation.ping");
    window = CreateWindowExW(0, cls.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr,
                             nullptr, cls.hInstance, nullptr);
    DHEPZ_CHECK(window != nullptr);
  }

  ~Rig() {
    if (window != nullptr) DestroyWindow(window);
    g_state = nullptr;
  }
};

template <typename Predicate>
void PumpUntil(Predicate predicate, int timeout_ms) {
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

std::wstring TempDir() {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  const std::wstring path = std::wstring(base) + L"dhepz-host-probe-" +
                            std::to_wstring(GetTickCount64());
  CreateDirectoryW(path.c_str(), nullptr);
  return path;
}

std::wstring g_gate_probe_directory;

class HostConsumerModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"host-consumer"; }
  std::wstring_view TabLabel() const override { return L"Host Consumer"; }
  int Order() const override { return 10; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return {L"host:probe"}; }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }
  core::Status Bind(modules::ModuleHost& host) override {
    host_ = &host;
    return core::Ok();
  }
  core::Status Handle(std::wstring_view, const json::Value&, json::Value*) override {
    modules::FolderProbeRequest request;
    request.directory = g_gate_probe_directory;
    request.relative_files = {L"present.txt"};
    return host_->StartFolderProbe(
        request,
        [this](const modules::HostOperationCompletion& completion) {
          json::Value patch = json::Value::Object();
          patch.Set(L"present", json::Value::Bool(completion.folder.files[0].present));
          publish_status_ = host_->PublishStatePatch(patch);
        },
        &token_);
  }
  void Release() override {}

  modules::ModuleHost* host_ = nullptr;
  modules::AsyncRequestToken token_;
  core::Status publish_status_;
};

std::unique_ptr<modules::ModuleDescriptor> MakeHostConsumer() {
  return std::make_unique<HostConsumerModule>();
}

std::wstring HostConsumerEmbedded() {
  return LR"({
    "core": {
      "schema": "dhepz.ui.core", "version": 1,
      "tokens": { "dark": { "text": "#ffffff" }, "light": { "text": "#000000" } },
      "allows_children": ["screen"],
      "components": {
        "screen": { "properties": {
          "route_id": { "kind": "string", "required": true },
          "module_id": { "kind": "string" },
          "tab_label": { "kind": "string" }
        } }
      }
    },
    "components": [
      { "type": "screen", "route_id": "host-home", "module_id": "host-consumer",
        "tab_label": "Host Consumer" }
    ],
    "modules": [
      { "moduleId": "host-consumer", "tabLabel": "Host Consumer",
        "order": 10, "showInTabs": true, "actions": ["host:probe"] }
    ]
  })";
}

}  // namespace

DHEPZ_TEST(HostOperations, CaptureDoesNotBlockUiAndCompletesOnUiThread) {
  Rig rig;
  modules::HostOperationDispatcher dispatcher(rig.window, rig.state.completion_message, {});
  const unsigned long ui_thread = GetCurrentThreadId();
  std::atomic<bool> delivered{false};
  modules::HostOperationCompletion observed;
  unsigned long delivery_thread = 0;

  modules::ProcessRequest request;
  request.operation = modules::ProcessOperation::Capture;
  request.executable = L"powershell.exe";
  request.arguments = {L"-NoProfile", L"-Command",
                       L"Start-Sleep -Seconds 2; [Console]::Out.Write('host-ok')"};
  modules::AsyncRequestToken token;
  DHEPZ_CHECK(dispatcher.StartProcess(
                  request,
                  [&](const modules::HostOperationCompletion& completion) {
                    observed = completion;
                    delivery_thread = GetCurrentThreadId();
                    delivered = true;
                  },
                  &token)
                  .ok());
  DHEPZ_CHECK(token);

  PostMessageW(rig.window, rig.state.ping_message, 0, 0);
  PumpUntil([&] { return rig.state.pings.load() == 1; }, 500);
  DHEPZ_CHECK_EQ(rig.state.pings.load(), 1);
  DHEPZ_CHECK(!delivered.load());

  PumpUntil([&] { return delivered.load(); }, 10000);
  DHEPZ_CHECK(delivered.load());
  DHEPZ_CHECK_EQ(static_cast<std::uint64_t>(delivery_thread),
                 static_cast<std::uint64_t>(ui_thread));
  DHEPZ_CHECK(observed.token == token);
  DHEPZ_CHECK(observed.generation != 0);
  DHEPZ_CHECK(observed.kind == modules::HostOperationKind::Capture);
  DHEPZ_CHECK(observed.status.ok());
  DHEPZ_CHECK(observed.process.output.find(L"host-ok") != std::wstring::npos);

  PumpUntil(
      [&] {
        dispatcher.ReapFinished();
        return dispatcher.ThreadCount() == 0;
      },
      1000);
  DHEPZ_CHECK_EQ(dispatcher.ThreadCount(), static_cast<std::size_t>(0));
}

DHEPZ_TEST(HostOperations, AppGateHostDelegatesOperationsAndStatePatches) {
  Rig rig;
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"host-consumer", &MakeHostConsumer);
  g_gate_probe_directory = TempDir();
  HANDLE file = CreateFileW((g_gate_probe_directory + L"\\present.txt").c_str(), GENERIC_WRITE,
                            0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  DHEPZ_CHECK(file != INVALID_HANDLE_VALUE);
  CloseHandle(file);

  std::atomic<bool> published{false};
  modules::AppGate gate;
  DHEPZ_CHECK(gate.ConfigureHostOperations(
                  rig.window, rig.state.completion_message,
                  [&](std::wstring_view module_id, const json::Value& patch) {
                    DHEPZ_CHECK_EQ(std::wstring(module_id), std::wstring(L"host-consumer"));
                    const json::Value* present = patch.Find(L"present");
                    DHEPZ_CHECK(present != nullptr && present->AsBool());
                    published = true;
                    return core::Ok();
                  })
                  .ok());
  DHEPZ_CHECK(gate.StartWithEmbedded(HostConsumerEmbedded()).ok());
  json::Value payload = json::Value::Object();
  json::Value immediate = json::Value::Object();
  DHEPZ_CHECK(gate.Dispatch(L"host:probe", payload, &immediate).ok());
  PumpUntil([&] { return published.load(); }, 2000);
  DHEPZ_CHECK(published.load());
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(HostOperations, CancelAndGenerationInvalidationSuppressCompletion) {
  Rig rig;
  modules::HostOperationDispatcher dispatcher(rig.window, rig.state.completion_message, {});
  std::atomic<int> deliveries{0};
  modules::ProcessRequest request;
  request.operation = modules::ProcessOperation::Capture;
  request.executable = L"powershell.exe";
  request.arguments = {L"-NoProfile", L"-Command", L"Start-Sleep -Seconds 5"};

  modules::AsyncRequestToken cancelled;
  DHEPZ_CHECK(dispatcher.StartProcess(
                  request,
                  [&](const modules::HostOperationCompletion&) { ++deliveries; }, &cancelled)
                  .ok());
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  dispatcher.CancelRequest(cancelled);
  PumpUntil(
      [&] {
        dispatcher.ReapFinished();
        return dispatcher.ThreadCount() == 0;
      },
      1000);
  DHEPZ_CHECK_EQ(dispatcher.ThreadCount(), static_cast<std::size_t>(0));
  DHEPZ_CHECK_EQ(deliveries.load(), 0);

  request.arguments = {L"-NoProfile", L"-Command", L"Start-Sleep -Milliseconds 300"};
  modules::AsyncRequestToken stale;
  DHEPZ_CHECK(dispatcher.StartProcess(
                  request,
                  [&](const modules::HostOperationCompletion&) { ++deliveries; }, &stale)
                  .ok());
  dispatcher.InvalidateGeneration();
  PumpUntil([&] { return false; }, 500);
  DHEPZ_CHECK_EQ(deliveries.load(), 0);
}

DHEPZ_TEST(HostOperations, ParentQuotingRoundTripsStructuredArguments) {
  modules::ProcessRequest request;
  request.executable = L"C:\\Program Files\\Tool\\tool.exe";
  request.arguments = {L"space value", L"&literal", L"quote\"value", L"trailing\\"};

  std::wstring command_line;
  DHEPZ_CHECK(modules::BuildProcessCommandLine(request, &command_line).ok());
  int count = 0;
  wchar_t** parsed = CommandLineToArgvW(command_line.c_str(), &count);
  DHEPZ_CHECK(parsed != nullptr);
  DHEPZ_CHECK_EQ(count, 5);
  DHEPZ_CHECK_EQ(std::wstring(parsed[0]), request.executable);
  for (int i = 1; i < count; ++i) {
    DHEPZ_CHECK_EQ(std::wstring(parsed[i]), request.arguments[static_cast<std::size_t>(i - 1)]);
  }
  LocalFree(parsed);
}

DHEPZ_TEST(HostOperations, ParentQuotingRejectsEmbeddedNullArguments) {
  modules::ProcessRequest request;
  request.executable = L"tool.exe";
  request.arguments = {std::wstring(L"before\0after", 12)};
  std::wstring command_line;
  DHEPZ_CHECK(!modules::BuildProcessCommandLine(request, &command_line).ok());
  DHEPZ_CHECK(command_line.empty());
}

DHEPZ_TEST(HostOperations, FolderProbeReturnsOnlyRequestedMetadata) {
  Rig rig;
  modules::HostOperationDispatcher dispatcher(rig.window, rig.state.completion_message, {});
  const std::wstring directory = TempDir();
  CreateDirectoryW((directory + L"\\Scripts").c_str(), nullptr);
  HANDLE file = CreateFileW((directory + L"\\Scripts\\Activate.ps1").c_str(), GENERIC_WRITE,
                            0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  DHEPZ_CHECK(file != INVALID_HANDLE_VALUE);
  CloseHandle(file);

  modules::FolderProbeRequest request;
  request.directory = directory;
  request.relative_files = {L"Scripts\\Activate.ps1", L"Scripts\\activate.bat"};
  modules::HostOperationCompletion observed;
  std::atomic<bool> delivered{false};
  modules::AsyncRequestToken token;
  DHEPZ_CHECK(dispatcher.StartFolderProbe(
                  request,
                  [&](const modules::HostOperationCompletion& completion) {
                    observed = completion;
                    delivered = true;
                  },
                  &token)
                  .ok());
  PumpUntil([&] { return delivered.load(); }, 2000);
  DHEPZ_CHECK(delivered.load());
  DHEPZ_CHECK(observed.status.ok());
  DHEPZ_CHECK(observed.kind == modules::HostOperationKind::FolderProbe);
  DHEPZ_CHECK(observed.folder.directory_exists);
  DHEPZ_CHECK_EQ(observed.folder.files.size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK(observed.folder.files[0].present);
  DHEPZ_CHECK(!observed.folder.files[1].present);
}

DHEPZ_TEST(HostOperations, StatePatchPublicationIsUiThreadOnly) {
  Rig rig;
  std::atomic<int> publications{0};
  modules::HostOperationDispatcher dispatcher(
      rig.window, rig.state.completion_message,
      [&](const json::Value&) {
        ++publications;
        return core::Ok();
      });
  const json::Value patch = json::Value::Object();
  DHEPZ_CHECK(dispatcher.PublishStatePatch(patch).ok());

  core::Status background;
  std::thread other([&] { background = dispatcher.PublishStatePatch(patch); });
  other.join();
  DHEPZ_CHECK(!background.ok());
  DHEPZ_CHECK_EQ(publications.load(), 1);
}

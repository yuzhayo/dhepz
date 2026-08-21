#include "modules/terminal/terminal_module.h"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>

#include "framework/test_case.h"
#include "modules/contract/module_manifest.h"
#include "modules/registry/module_registry.h"
#include "modules/terminal/terminal_logic.h"

namespace {

class FakeLaunchHost final : public modules::ModuleHost {
 public:
  modules::ModuleSurface Surface() override { return {}; }
  core::Status SettingsRead(std::wstring_view, std::wstring*) override {
    return core::Err(core::ErrorCode::NotFound, L"not configured");
  }
  core::Status SettingsReadGlobal(std::wstring_view, std::wstring*) override {
    return core::Err(core::ErrorCode::NotFound, L"not configured");
  }
  core::Status SettingsWrite(std::wstring_view, std::wstring_view) override {
    return core::Ok();
  }
  core::Status StartSettingsLoad(modules::HostOperationCallback,
                                 modules::AsyncRequestToken*) override {
    return core::Err(core::ErrorCode::Unsupported, L"not configured");
  }
  core::Status StartProcess(const modules::ProcessRequest& value,
                            modules::HostOperationCallback completed,
                            modules::AsyncRequestToken* token) override {
    ++process_calls;
    request = value;
    callback = std::move(completed);
    token->value = 77;
    return start_status;
  }
  core::Status StartFolderProbe(const modules::FolderProbeRequest&,
                                modules::HostOperationCallback,
                                modules::AsyncRequestToken*) override {
    return core::Err(core::ErrorCode::Unsupported, L"not configured");
  }
  void CancelRequest(modules::AsyncRequestToken token) override {
    cancelled = token;
  }
  core::Status PublishStatePatch(const json::Value& patch) override {
    patches.push_back(patch);
    return core::Ok();
  }
  core::Status GetSettingsAllFacet(modules::SettingsAllFacet** facet) override {
    *facet = nullptr;
    return core::Err(core::ErrorCode::PermissionDenied, L"not granted");
  }
  core::Status GetConfigWriteFacet(modules::ConfigWriteFacet** facet) override {
    *facet = nullptr;
    return core::Err(core::ErrorCode::PermissionDenied, L"not granted");
  }
  void ReportStatus(const core::Status& value) override { reported = value; }
  void Log(std::wstring_view, std::wstring_view) override {}
  core::Status RequestRoute(std::wstring_view) override { return core::Ok(); }
  std::vector<modules::PeerInfo> Peers() override { return {}; }

  core::Status start_status;
  modules::ProcessRequest request;
  modules::HostOperationCallback callback;
  modules::AsyncRequestToken cancelled;
  std::vector<json::Value> patches;
  core::Status reported;
  int process_calls = 0;
};

std::wstring Ws(const std::string& narrow) {
  std::wstring wide;
  for (const char c : narrow) wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
  return wide;
}

std::string ReadFile(const std::wstring& path) {
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream text;
  text << stream.rdbuf();
  return text.str();
}

std::wstring RepoRoot() {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  std::wstring path(buffer);
  for (int i = 0; i < 4; ++i) {
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    path.resize(slash);
  }
  return path;
}

}  // namespace

DHEPZ_TEST(TerminalScaffold, ManifestValidWithNoCapabilities) {
  const std::wstring root = RepoRoot();
  const std::string manifest_text =
      ReadFile(root + L"\\src\\modules\\terminal\\module.json");
  DHEPZ_CHECK(!manifest_text.empty());
  modules::ModuleManifest manifest;
  std::vector<modules::ManifestDiagnostic> diagnostics;
  DHEPZ_CHECK(
      modules::ParseManifest(Ws(manifest_text), &manifest, &diagnostics).ok());
  DHEPZ_CHECK_EQ(manifest.module_id, std::wstring(L"terminal"));
  DHEPZ_CHECK(manifest.capabilities.empty());
  DHEPZ_CHECK(manifest.show_in_tabs);
}

DHEPZ_TEST(TerminalScaffold, SelfRegistersAndIsDiscoverable) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"terminal", &terminal::MakeTerminalForTests);
  bool found = false;
  for (const modules::RegisteredModule& module : modules::CollectModules()) {
    if (module.module_id == L"terminal") found = true;
  }
  DHEPZ_CHECK(found);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(TerminalLogic, WslBuilderKeepsDistroAsOneStructuredArgument) {
  terminal::LaunchSpec spec;
  spec.shell = terminal::Shell::Wsl;
  spec.wsl_distro = L"My Distro";
  modules::ProcessRequest request;
  DHEPZ_CHECK(terminal::BuildProcessRequest(spec, &request).ok());
  DHEPZ_CHECK_EQ(request.executable, std::wstring(L"wsl.exe"));
  DHEPZ_CHECK_EQ(request.arguments.size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(request.arguments[1], std::wstring(L"My Distro"));
}

DHEPZ_TEST(TerminalLogic, TypedPayloadBuildsEveryShellAndElevationMode) {
  struct Case {
    const wchar_t* json;
    const wchar_t* executable;
    modules::ProcessOperation operation;
  };
  const Case cases[] = {
      {LR"({"shell":"powershell","admin":false,"working_folder":"C:\\work","venv":null})",
       L"powershell.exe", modules::ProcessOperation::Launch},
      {LR"({"shell":"cmd","admin":true,"working_folder":"C:\\work","venv":null})",
       L"cmd.exe", modules::ProcessOperation::ElevatedLaunch},
      {LR"({"shell":"wsl","wsl_distro":"Ubuntu","admin":false,"working_folder":"/work","venv":null})",
       L"wsl.exe", modules::ProcessOperation::Launch},
  };
  for (const Case& item : cases) {
    json::Value payload;
    DHEPZ_CHECK(json::Parse(item.json, &payload).ok());
    terminal::LaunchSpec spec;
    DHEPZ_CHECK(terminal::ParseLaunchPayload(payload, &spec).ok());
    modules::ProcessRequest request;
    DHEPZ_CHECK(terminal::BuildProcessRequest(spec, &request).ok());
    DHEPZ_CHECK_EQ(request.executable, std::wstring(item.executable));
    DHEPZ_CHECK(request.operation == item.operation);
  }
}

DHEPZ_TEST(TerminalLogic, ShellSpecificVenvArgumentsAndInvalidCombinations) {
  terminal::LaunchSpec powershell;
  powershell.shell = terminal::Shell::PowerShell;
  powershell.venv = {terminal::PathKind::Windows,
                     L"C:\\work\\.venv\\Scripts\\Activate.ps1"};
  modules::ProcessRequest request;
  DHEPZ_CHECK(terminal::BuildProcessRequest(powershell, &request).ok());
  DHEPZ_CHECK_EQ(request.arguments.back(), powershell.venv.activate_path);

  terminal::LaunchSpec cmd = powershell;
  cmd.shell = terminal::Shell::Cmd;
  cmd.venv.activate_path = L"C:\\work\\.venv\\Scripts\\activate.bat";
  DHEPZ_CHECK(terminal::BuildProcessRequest(cmd, &request).ok());
  DHEPZ_CHECK_EQ(request.arguments[0], std::wstring(L"/K"));

  terminal::LaunchSpec wsl;
  wsl.shell = terminal::Shell::Wsl;
  wsl.wsl_distro = L"Ubuntu";
  wsl.venv = {terminal::PathKind::Linux, L"/work/.venv/bin/activate"};
  DHEPZ_CHECK(terminal::BuildProcessRequest(wsl, &request).ok());
  DHEPZ_CHECK_EQ(request.arguments.back(), wsl.venv.activate_path);

  wsl.working_dir = L"C:\\work";
  DHEPZ_CHECK_FALSE(terminal::BuildProcessRequest(wsl, &request).ok());
  wsl.working_dir.clear();
  wsl.admin = true;
  DHEPZ_CHECK_FALSE(terminal::BuildProcessRequest(wsl, &request).ok());
  powershell.venv = {terminal::PathKind::Linux, L"/work/.venv/bin/activate"};
  DHEPZ_CHECK_FALSE(terminal::BuildProcessRequest(powershell, &request).ok());
}

DHEPZ_TEST(TerminalLogic, DisabledVenvDoesNotReachTheProcessRequest) {
  json::Value payload;
  DHEPZ_CHECK(json::Parse(
      LR"({"shell":"powershell","venv_enabled":false,"venv":{"kind":"windows","activate_path":"C:\\work\\.venv\\Scripts\\Activate.ps1"}})",
      &payload).ok());
  terminal::LaunchSpec spec;
  DHEPZ_CHECK(terminal::ParseLaunchPayload(payload, &spec).ok());
  DHEPZ_CHECK(spec.venv.kind == terminal::PathKind::None);
  modules::ProcessRequest request;
  DHEPZ_CHECK(terminal::BuildProcessRequest(spec, &request).ok());
  DHEPZ_CHECK_EQ(request.arguments.size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK_EQ(request.arguments[0], std::wstring(L"-NoExit"));
}

DHEPZ_TEST(TerminalLogic, InvalidPayloadFailsBeforeARequestCanBeBuilt) {
  const wchar_t* cases[] = {
      LR"({})", LR"({"shell":"fish"})", LR"({"shell":"wsl"})",
      LR"({"shell":"cmd","admin":"yes"})",
      LR"({"shell":"cmd","surprise":true})",
      LR"({"shell":"powershell","venv":{"kind":"windows"}})"};
  for (const wchar_t* text : cases) {
    json::Value payload;
    DHEPZ_CHECK(json::Parse(text, &payload).ok());
    terminal::LaunchSpec spec;
    const core::Status parsed = terminal::ParseLaunchPayload(payload, &spec);
    if (std::wstring_view(text).find(L"\"shell\":\"wsl\"") !=
        std::wstring_view::npos && parsed.ok()) {
      modules::ProcessRequest request;
      DHEPZ_CHECK_FALSE(terminal::BuildProcessRequest(spec, &request).ok());
    } else {
      DHEPZ_CHECK_FALSE(parsed.ok());
    }
  }
}

DHEPZ_TEST(TerminalModule, UsesHostRequestAndPublishesCancelledCompletion) {
  FakeLaunchHost host;
  const std::unique_ptr<modules::ModuleDescriptor> module =
      terminal::MakeTerminalForTests();
  DHEPZ_CHECK(module->Bind(host).ok());
  DHEPZ_CHECK_EQ(host.patches.size(), static_cast<std::size_t>(1));

  json::Value payload;
  DHEPZ_CHECK(json::Parse(
      LR"({"shell":"cmd","admin":true,"working_folder":"C:\\work","venv_enabled":false,"venv":null})",
      &payload).ok());
  json::Value immediate;
  DHEPZ_CHECK(module->Handle(L"terminal:launch", payload, &immediate).ok());
  DHEPZ_CHECK_EQ(host.process_calls, 1);
  DHEPZ_CHECK_EQ(host.request.executable, std::wstring(L"cmd.exe"));
  DHEPZ_CHECK(host.request.operation == modules::ProcessOperation::ElevatedLaunch);
  DHEPZ_CHECK_EQ(host.request.working_directory, std::wstring(L"C:\\work"));
  DHEPZ_CHECK(immediate.BoolField(L"busy"));

  modules::HostOperationCompletion completion;
  completion.token.value = 77;
  completion.kind = modules::HostOperationKind::ElevatedLaunch;
  completion.status = core::Err(core::ErrorCode::Cancelled, L"UAC was cancelled");
  host.callback(completion);
  DHEPZ_CHECK(host.reported.Code() == core::ErrorCode::Cancelled);
  DHEPZ_CHECK_EQ(host.patches.size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_FALSE(host.patches.back().BoolField(L"busy"));
  DHEPZ_CHECK_CONTAINS(host.patches.back().StringField(L"status"),
                       std::wstring(L"UAC"));
  module->Release();
}

DHEPZ_TEST(TerminalModule, InvalidPayloadNeverInvokesHost) {
  FakeLaunchHost host;
  const std::unique_ptr<modules::ModuleDescriptor> module =
      terminal::MakeTerminalForTests();
  DHEPZ_CHECK(module->Bind(host).ok());
  json::Value payload;
  DHEPZ_CHECK(json::Parse(LR"({"shell":"wsl","admin":true})", &payload).ok());
  json::Value patch;
  const core::Status status =
      module->Handle(L"terminal:launch", payload, &patch);
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(host.process_calls, 0);
  module->Release();
}

#include "modules/terminal/terminal_module.h"

#include <windows.h>

#include <fstream>
#include <map>
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
  core::Status SettingsRead(std::wstring_view key, std::wstring* out) override {
    const auto found = settings.find(std::wstring(key));
    if (found == settings.end()) {
      return core::Err(core::ErrorCode::NotFound, L"not configured");
    }
    *out = found->second;
    return core::Ok();
  }
  core::Status SettingsReadGlobal(std::wstring_view, std::wstring*) override {
    return core::Err(core::ErrorCode::NotFound, L"not configured");
  }
  core::Status SettingsWrite(std::wstring_view key,
                             std::wstring_view value) override {
    ++settings_writes;
    settings[std::wstring(key)] = std::wstring(value);
    return core::Ok();
  }
  core::Status StartSettingsLoad(modules::HostOperationCallback completed,
                                 modules::AsyncRequestToken* token) override {
    if (!settings_enabled) {
      return core::Err(core::ErrorCode::Unsupported, L"not configured");
    }
    settings_callback = std::move(completed);
    token->value = 10;
    return core::Ok();
  }
  core::Status StartProcess(const modules::ProcessRequest& value,
                            modules::HostOperationCallback completed,
                            modules::AsyncRequestToken* token) override {
    if (value.operation == modules::ProcessOperation::Capture) {
      ++capture_calls;
      capture_request = value;
      capture_callback = std::move(completed);
      token->value = next_token++;
      capture_token = *token;
      return core::Ok();
    }
    ++process_calls;
    request = value;
    callback = std::move(completed);
    token->value = next_token++;
    process_token = *token;
    return start_status;
  }
  core::Status StartFolderProbe(const modules::FolderProbeRequest& value,
                                modules::HostOperationCallback completed,
                                modules::AsyncRequestToken* token) override {
    ++folder_calls;
    folder_request = value;
    folder_callback = std::move(completed);
    token->value = next_token++;
    folder_token = *token;
    return core::Ok();
  }
  core::Status PickFolder(const modules::FolderPickerRequest& value,
                          modules::FolderPickerResult* result) override {
    picker_request = value;
    if (!picker_status.ok()) return picker_status;
    result->directory = picker_result;
    return core::Ok();
  }
  void CancelRequest(modules::AsyncRequestToken token) override {
    cancelled.push_back(token);
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

  void CompleteSettings(const core::Status& status = core::Ok()) {
    modules::HostOperationCompletion completion;
    completion.token.value = 10;
    completion.kind = modules::HostOperationKind::SettingsLoad;
    completion.status = status;
    settings_callback(completion);
  }

  void CompleteProcess(const core::Status& status) {
    modules::HostOperationCompletion completion;
    completion.token = process_token;
    completion.generation = 1;
    completion.kind = request.operation == modules::ProcessOperation::ElevatedLaunch
                          ? modules::HostOperationKind::ElevatedLaunch
                          : modules::HostOperationKind::Launch;
    completion.status = status;
    callback(completion);
  }

  void CompleteCapture(const core::Status& status, std::wstring output,
                       int exit_code = 0) {
    modules::HostOperationCompletion completion;
    completion.token = capture_token;
    completion.generation = 1;
    completion.kind = modules::HostOperationKind::Capture;
    completion.status = status;
    completion.process.output = std::move(output);
    completion.process.exit_code = exit_code;
    capture_callback(completion);
  }

  void CompleteFolder(const core::Status& status,
                      modules::FolderProbeResult folder) {
    modules::HostOperationCompletion completion;
    completion.token = folder_token;
    completion.generation = 1;
    completion.kind = modules::HostOperationKind::FolderProbe;
    completion.status = status;
    completion.folder = std::move(folder);
    folder_callback(completion);
  }

  core::Status start_status;
  modules::ProcessRequest request;
  modules::AsyncRequestToken process_token;
  modules::HostOperationCallback callback;
  modules::ProcessRequest capture_request;
  modules::HostOperationCallback capture_callback;
  modules::AsyncRequestToken capture_token;
  modules::HostOperationCallback settings_callback;
  modules::FolderProbeRequest folder_request;
  modules::HostOperationCallback folder_callback;
  modules::AsyncRequestToken folder_token;
  modules::FolderPickerRequest picker_request;
  std::wstring picker_result;
  core::Status picker_status;
  std::vector<modules::AsyncRequestToken> cancelled;
  std::vector<json::Value> patches;
  core::Status reported;
  int process_calls = 0;
  int capture_calls = 0;
  int folder_calls = 0;
  int settings_writes = 0;
  std::uint64_t next_token = 77;
  bool settings_enabled = false;
  std::map<std::wstring, std::wstring> settings;
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
  DHEPZ_CHECK_FALSE(manifest.show_in_tabs);
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
  spec.target = terminal::Target::Wsl;
  spec.working_folder = L"C:\\work";
  spec.wsl_distro = L"My Distro";
  modules::ProcessRequest request;
  DHEPZ_CHECK(terminal::BuildProcessRequest(spec, &request).ok());
  DHEPZ_CHECK_EQ(request.executable, std::wstring(L"wt.exe"));
  DHEPZ_CHECK(std::find(request.arguments.begin(), request.arguments.end(),
                        std::wstring(L"My Distro")) != request.arguments.end());
}

DHEPZ_TEST(TerminalLogic, TypedPayloadBuildsEveryApprovedTarget) {
  struct Case {
    const wchar_t* json;
    const wchar_t* executable;
    modules::ProcessOperation operation;
  };
  const Case cases[] = {
      {LR"({"target":"powershell","working_folder":"C:\\work"})",
       L"wt.exe", modules::ProcessOperation::Launch},
      {LR"({"target":"powershell_admin","working_folder":"C:\\work"})",
       L"wt.exe", modules::ProcessOperation::ElevatedLaunch},
      {LR"({"target":"wsl","wsl_distro":"Ubuntu","working_folder":"C:\\work"})",
       L"wt.exe", modules::ProcessOperation::Launch},
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

DHEPZ_TEST(TerminalLogic, VenvActivationIsOwnedByEachApprovedTarget) {
  terminal::LaunchSpec powershell;
  powershell.target = terminal::Target::PowerShell;
  powershell.working_folder = L"C:\\work";
  powershell.venv_enabled = true;
  modules::ProcessRequest request;
  DHEPZ_CHECK(terminal::BuildProcessRequest(powershell, &request).ok());
  DHEPZ_CHECK_EQ(request.arguments.back(),
                 std::wstring(L"C:\\work\\.venv\\Scripts\\Activate.ps1"));

  terminal::LaunchSpec wsl;
  wsl.target = terminal::Target::Wsl;
  wsl.working_folder = L"C:\\work";
  wsl.wsl_distro = L"Ubuntu";
  wsl.venv_enabled = true;
  DHEPZ_CHECK(terminal::BuildProcessRequest(wsl, &request).ok());
  DHEPZ_CHECK_CONTAINS(request.arguments.back(),
                       std::wstring(L".venv/bin/activate"));
}

DHEPZ_TEST(TerminalLogic, DisabledVenvDoesNotReachTheProcessRequest) {
  json::Value payload;
  DHEPZ_CHECK(json::Parse(
      LR"({"target":"powershell","working_folder":"C:\\work","venv_enabled":false})",
      &payload).ok());
  terminal::LaunchSpec spec;
  DHEPZ_CHECK(terminal::ParseLaunchPayload(payload, &spec).ok());
  DHEPZ_CHECK_FALSE(spec.venv_enabled);
  modules::ProcessRequest request;
  DHEPZ_CHECK(terminal::BuildProcessRequest(spec, &request).ok());
  DHEPZ_CHECK(std::find(request.arguments.begin(), request.arguments.end(),
                        std::wstring(L"powershell.exe")) ==
              request.arguments.end());
}

DHEPZ_TEST(TerminalLogic, InvalidPayloadFailsBeforeARequestCanBeBuilt) {
  const wchar_t* cases[] = {
      LR"({})", LR"({"target":"fish","working_folder":"C:\\work"})",
      LR"({"target":"wsl","working_folder":"C:\\work"})",
      LR"({"target":"cmd","working_folder":"C:\\work"})",
      LR"({"target":"powershell","working_folder":"C:\\work","surprise":true})"};
  for (const wchar_t* text : cases) {
    json::Value payload;
    DHEPZ_CHECK(json::Parse(text, &payload).ok());
    terminal::LaunchSpec spec;
    const core::Status parsed = terminal::ParseLaunchPayload(payload, &spec);
    DHEPZ_CHECK_FALSE(parsed.ok());
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
      LR"({"target":"powershell_admin","working_folder":"C:\\work","venv_enabled":false})",
      &payload).ok());
  json::Value immediate;
  DHEPZ_CHECK(module->Handle(L"terminal:launch", payload, &immediate).ok());
  DHEPZ_CHECK_EQ(host.folder_calls, 1);
  DHEPZ_CHECK_EQ(host.process_calls, 0);
  modules::FolderProbeResult folder;
  folder.normalized_directory = L"C:\\work";
  folder.directory_exists = true;
  host.CompleteFolder(core::Ok(), std::move(folder));
  DHEPZ_CHECK_EQ(host.process_calls, 1);
  DHEPZ_CHECK_EQ(host.request.executable, std::wstring(L"wt.exe"));
  DHEPZ_CHECK(host.request.operation == modules::ProcessOperation::ElevatedLaunch);
  DHEPZ_CHECK_EQ(host.request.working_directory, std::wstring(L"C:\\work"));
  DHEPZ_CHECK(immediate.BoolField(L"busy"));

  host.CompleteProcess(
      core::Err(core::ErrorCode::Cancelled, L"UAC was cancelled"));
  DHEPZ_CHECK(host.reported.Code() == core::ErrorCode::Cancelled);
  DHEPZ_CHECK_EQ(host.patches.size(), static_cast<std::size_t>(3));
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
  DHEPZ_CHECK(json::Parse(
      LR"({"target":"wsl","working_folder":"C:\\work"})", &payload).ok());
  json::Value patch;
  const core::Status status =
      module->Handle(L"terminal:launch", payload, &patch);
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(host.process_calls, 0);
  module->Release();
}

DHEPZ_TEST(TerminalModule, DefaultsRenderBeforeAsyncRecentFoldersLoad) {
  FakeLaunchHost host;
  host.settings_enabled = true;
  host.settings[L"recent_folders"] = L"[\"C:\\\\old\",\"C:\\\\new\"]";
  const auto module = terminal::MakeTerminalForTests();
  DHEPZ_CHECK(module->Bind(host).ok());
  DHEPZ_CHECK_EQ(host.patches.size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK_EQ(host.patches[0].StringField(L"status"), std::wstring(L"Ready"));
  DHEPZ_CHECK_EQ(host.patches[0].ArrayField(L"recent_folders")->size(),
                 static_cast<std::size_t>(0));

  host.CompleteSettings();
  DHEPZ_CHECK_EQ(host.patches.size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(host.patches.back().ArrayField(L"recent_folders")->size(),
                 static_cast<std::size_t>(2));
  module->Release();
}

DHEPZ_TEST(TerminalModule, WslCaptureUsesHostAndCacheSurvivesWindowRebind) {
  FakeLaunchHost first_host;
  const auto module = terminal::MakeTerminalForTests();
  DHEPZ_CHECK(module->Bind(first_host).ok());
  DHEPZ_CHECK_EQ(first_host.capture_calls, 1);
  DHEPZ_CHECK(first_host.capture_request.operation ==
              modules::ProcessOperation::Capture);
  DHEPZ_CHECK_EQ(first_host.capture_request.executable,
                 std::wstring(L"wsl.exe"));
  DHEPZ_CHECK_EQ(first_host.capture_request.arguments.size(),
                 static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(first_host.capture_request.arguments[0], std::wstring(L"-l"));
  DHEPZ_CHECK_EQ(first_host.capture_request.arguments[1], std::wstring(L"-q"));

  first_host.CompleteCapture(core::Ok(), L"Ubuntu\r\ndocker-desktop\r\n");
  DHEPZ_CHECK_EQ(first_host.patches.back().ArrayField(L"wsl_distros")->size(),
                 static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(first_host.patches.back().StringField(L"wsl_distro"),
                 std::wstring(L"Ubuntu"));
  module->Release();

  FakeLaunchHost rebound_host;
  DHEPZ_CHECK(module->Bind(rebound_host).ok());
  DHEPZ_CHECK_EQ(rebound_host.capture_calls, 0);
  DHEPZ_CHECK_EQ(rebound_host.patches.front().ArrayField(L"wsl_distros")->size(),
                 static_cast<std::size_t>(2));
  module->Release();
}

DHEPZ_TEST(TerminalModule, NewerWslRefreshWinsAndFailureRetainsCache) {
  FakeLaunchHost host;
  const auto module = terminal::MakeTerminalForTests();
  DHEPZ_CHECK(module->Bind(host).ok());
  host.CompleteCapture(core::Ok(), L"Ubuntu\n");
  host.patches.clear();

  json::Value immediate;
  DHEPZ_CHECK(module->Handle(L"terminal:refresh-wsl", json::Value::Object(),
                             &immediate).ok());
  const modules::HostOperationCallback stale_callback = host.capture_callback;
  const modules::AsyncRequestToken stale_token = host.capture_token;
  DHEPZ_CHECK(immediate.BoolField(L"busy"));
  DHEPZ_CHECK(module->Handle(L"terminal:refresh-wsl", json::Value::Object(),
                             &immediate).ok());
  DHEPZ_CHECK(std::find(host.cancelled.begin(), host.cancelled.end(), stale_token) !=
              host.cancelled.end());

  modules::HostOperationCompletion stale;
  stale.token = stale_token;
  stale.generation = 1;
  stale.kind = modules::HostOperationKind::Capture;
  stale.process.output = L"Stale\n";
  stale_callback(stale);
  DHEPZ_CHECK(host.patches.empty());

  host.CompleteCapture(
      core::Err(core::ErrorCode::IoError, L"wsl unavailable"), L"");
  DHEPZ_CHECK_EQ(host.reported.Code(), core::ErrorCode::IoError);
  DHEPZ_CHECK_EQ(host.reported.Message(), std::wstring(L"wsl unavailable"));
  DHEPZ_CHECK_EQ(host.patches.back().ArrayField(L"wsl_distros")->size(),
                 static_cast<std::size_t>(1));
  DHEPZ_CHECK_EQ(host.patches.back().StringField(L"wsl_distro"),
                 std::wstring(L"Ubuntu"));
  module->Release();
}

DHEPZ_TEST(TerminalModule, ReleaseCancelsWslAndDropsItsCompletion) {
  FakeLaunchHost host;
  const auto module = terminal::MakeTerminalForTests();
  DHEPZ_CHECK(module->Bind(host).ok());
  const modules::HostOperationCallback callback = host.capture_callback;
  const modules::AsyncRequestToken token = host.capture_token;
  host.patches.clear();
  module->Release();
  DHEPZ_CHECK(std::find(host.cancelled.begin(), host.cancelled.end(), token) !=
              host.cancelled.end());

  modules::HostOperationCompletion completion;
  completion.token = token;
  completion.generation = 1;
  completion.kind = modules::HostOperationKind::Capture;
  completion.process.output = L"Too late\n";
  callback(completion);
  DHEPZ_CHECK(host.patches.empty());
}

DHEPZ_TEST(TerminalModule, BrowseUsesNativeParentPickerAndReturnsSelection) {
  FakeLaunchHost host;
  host.picker_result = L"C:\\work";
  const auto module = terminal::MakeTerminalForTests();
  DHEPZ_CHECK(module->Bind(host).ok());
  json::Value payload;
  DHEPZ_CHECK(json::Parse(
      LR"({"initial_folder":"C:\\initial"})", &payload).ok());
  json::Value immediate;
  DHEPZ_CHECK(
      module->Handle(L"terminal:browse-folder", payload, &immediate).ok());
  DHEPZ_CHECK_EQ(host.picker_request.initial_directory,
                 std::wstring(L"C:\\initial"));
  DHEPZ_CHECK_EQ(immediate.StringField(L"working_folder"),
                 std::wstring(L"C:\\work"));
  module->Release();
}

DHEPZ_TEST(TerminalModule, EnabledVenvCreatesThenActivatesBeforeLaunch) {
  FakeLaunchHost host;
  const auto module = terminal::MakeTerminalForTests();
  DHEPZ_CHECK(module->Bind(host).ok());
  json::Value payload;
  DHEPZ_CHECK(json::Parse(
      LR"({"target":"powershell","working_folder":"C:\\work","venv_enabled":true})",
      &payload).ok());
  json::Value immediate;
  DHEPZ_CHECK(module->Handle(L"terminal:launch", payload, &immediate).ok());
  modules::FolderProbeResult first_probe;
  first_probe.normalized_directory = L"C:\\work";
  first_probe.directory_exists = true;
  first_probe.files = {{L".venv\\Scripts\\Activate.ps1", false}};
  host.CompleteFolder(core::Ok(), std::move(first_probe));
  DHEPZ_CHECK_EQ(host.capture_request.executable, std::wstring(L"py.exe"));
  host.CompleteCapture(core::Ok(), L"");
  modules::FolderProbeResult second_probe;
  second_probe.normalized_directory = L"C:\\work";
  second_probe.directory_exists = true;
  second_probe.files = {{L".venv\\Scripts\\Activate.ps1", true}};
  host.CompleteFolder(core::Ok(), std::move(second_probe));
  DHEPZ_CHECK_EQ(host.process_calls, 1);
  DHEPZ_CHECK_EQ(host.request.executable, std::wstring(L"wt.exe"));
  DHEPZ_CHECK_EQ(host.request.arguments.back(),
                 std::wstring(L"C:\\work\\.venv\\Scripts\\Activate.ps1"));
  module->Release();
}

DHEPZ_TEST(TerminalModule, OnlySuccessfulSpawnPersistsRecentFolder) {
  FakeLaunchHost host;
  const auto module = terminal::MakeTerminalForTests();
  DHEPZ_CHECK(module->Bind(host).ok());
  host.patches.clear();
  json::Value payload;
  DHEPZ_CHECK(json::Parse(
      LR"({"target":"powershell","working_folder":"C:\\work"})", &payload).ok());
  json::Value immediate;
  DHEPZ_CHECK(module->Handle(L"terminal:launch", payload, &immediate).ok());
  modules::FolderProbeResult folder;
  folder.normalized_directory = L"C:\\work";
  folder.directory_exists = true;
  host.CompleteFolder(core::Ok(), folder);
  host.CompleteProcess(core::Err(core::ErrorCode::Cancelled, L"cancelled"));
  DHEPZ_CHECK_EQ(host.settings_writes, 1);
  DHEPZ_CHECK(host.patches.back().Find(L"recent_folders") == nullptr);

  DHEPZ_CHECK(module->Handle(L"terminal:launch", payload, &immediate).ok());
  host.CompleteFolder(core::Ok(), std::move(folder));
  host.CompleteProcess(core::Ok());
  DHEPZ_CHECK_EQ(host.settings_writes, 3);
  DHEPZ_CHECK_EQ(host.patches.back().ArrayField(L"recent_folders")->size(),
                 static_cast<std::size_t>(1));
  module->Release();
}

DHEPZ_TEST(TerminalModule, ReleaseInvalidatesOutstandingLaunchProbe) {
  FakeLaunchHost host;
  const auto module = terminal::MakeTerminalForTests();
  DHEPZ_CHECK(module->Bind(host).ok());
  host.patches.clear();
  json::Value payload;
  DHEPZ_CHECK(json::Parse(
      LR"({"target":"powershell","working_folder":"C:\\work"})", &payload).ok());
  json::Value immediate;
  DHEPZ_CHECK(module->Handle(L"terminal:launch", payload, &immediate).ok());
  const auto callback = host.folder_callback;
  const auto token = host.folder_token;
  module->Release();
  modules::HostOperationCompletion completion;
  completion.token = token;
  completion.kind = modules::HostOperationKind::FolderProbe;
  completion.folder.directory_exists = true;
  completion.folder.normalized_directory = L"C:\\work";
  callback(completion);
  DHEPZ_CHECK(host.patches.empty());
}

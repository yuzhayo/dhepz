#include "modules/contract/module_contract.h"
#include "modules/contract/module_manifest.h"

#include <memory>
#include <string>
#include <utility>

#include "framework/test_case.h"

namespace {

std::wstring W(const char* utf8) {
  std::wstring wide;
  for (const char* p = utf8; *p != '\0'; ++p) {
    wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
  }
  return wide;
}

const char* kGood = R"({
  "moduleId": "terminal",
  "tabLabel": "Terminal",
  "order": 10,
  "showInTabs": true,
  "settingsRoute": "terminal-settings",
  "actions": ["launch", "focus"],
  "bindings": ["lastFolder"],
  "capabilities": []
})";

struct TestHost final : modules::ModuleHost {
  modules::ModuleSurface Surface() override { return {}; }
  core::Status SettingsRead(std::wstring_view, std::wstring* out) override {
    *out = L"v";
    return core::Ok();
  }
  core::Status SettingsReadGlobal(std::wstring_view, std::wstring* out) override {
    *out = L"dark";
    return core::Ok();
  }
  core::Status SettingsWrite(std::wstring_view key, std::wstring_view) override {
    last_write = std::wstring(key);
    return core::Ok();
  }
  core::Status StorageWrite(std::wstring_view, std::wstring_view) override { return core::Ok(); }
  core::Status StorageRead(std::wstring_view, std::wstring*) override { return core::Ok(); }
  core::Status StartProcess(const modules::ProcessRequest& request,
                            modules::HostOperationCallback callback,
                            modules::AsyncRequestToken* token) override {
    process_request = request;
    process_callback = std::move(callback);
    *token = {17};
    return core::Ok();
  }
  core::Status StartFolderProbe(const modules::FolderProbeRequest& request,
                                modules::HostOperationCallback callback,
                                modules::AsyncRequestToken* token) override {
    folder_request = request;
    folder_callback = std::move(callback);
    *token = {18};
    return core::Ok();
  }
  void CancelRequest(modules::AsyncRequestToken token) override { cancelled = token; }
  core::Status PublishStatePatch(const json::Value& patch) override {
    published_patch = patch;
    return core::Ok();
  }
  void ReportStatus(const core::Status& s) override { reported = !s.ok(); }
  void Log(std::wstring_view, std::wstring_view) override {}
  core::Status RequestRoute(std::wstring_view route) override {
    requested = std::wstring(route);
    return core::Ok();
  }
  std::vector<modules::PeerInfo> Peers() override { return {}; }

  std::wstring last_write;
  std::wstring requested;
  modules::ProcessRequest process_request;
  modules::FolderProbeRequest folder_request;
  modules::HostOperationCallback process_callback;
  modules::HostOperationCallback folder_callback;
  modules::AsyncRequestToken cancelled;
  json::Value published_patch;
  bool reported = false;
};

class TestModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"terminal"; }
  std::wstring_view TabLabel() const override { return L"Terminal"; }
  int Order() const override { return 10; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return L"terminal-settings"; }
  std::vector<std::wstring> DeclaredActions() const override { return {L"launch"}; }
  std::vector<std::wstring> DeclaredBindings() const override { return {L"lastFolder"}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }
  core::Status Bind(modules::ModuleHost& host) override {
    host_ = &host;
    return core::Ok();
  }
  core::Status Handle(std::wstring_view action, const json::Value&,
                      json::Value*) override {
    handled = std::wstring(action);
    if (action != L"launch") {
      return core::Err(core::ErrorCode::NotFound, L"undeclared action");
    }
    return core::Ok();
  }
  void Release() override { released = true; }

  modules::ModuleHost* host_ = nullptr;
  std::wstring handled;
  bool released = false;
};

}  // namespace

DHEPZ_TEST(ModuleManifest, ValidManifestParses) {
  modules::ModuleManifest manifest;
  std::vector<modules::ManifestDiagnostic> diagnostics;
  DHEPZ_CHECK(modules::ParseManifest(W(kGood), &manifest, &diagnostics).ok());
  DHEPZ_CHECK_EQ(manifest.module_id, std::wstring(L"terminal"));
  DHEPZ_CHECK_EQ(manifest.tab_label, std::wstring(L"Terminal"));
  DHEPZ_CHECK_EQ(manifest.order, 10);
  DHEPZ_CHECK(manifest.show_in_tabs);
  DHEPZ_CHECK_EQ(manifest.settings_route, std::wstring(L"terminal-settings"));
  DHEPZ_CHECK_EQ(manifest.actions.size(), static_cast<std::size_t>(2));
}

DHEPZ_TEST(ModuleContract, AsyncOperationsAreTypedAndTokened) {
  TestHost host;
  modules::ProcessRequest process;
  process.operation = modules::ProcessOperation::Capture;
  process.executable = L"tool.exe";
  process.arguments = {L"space value", L"&literal"};
  process.working_directory = L"C:\\work";
  process.timeout_ms = 2500;

  modules::AsyncRequestToken process_token;
  DHEPZ_CHECK(host.StartProcess(process, {}, &process_token).ok());
  DHEPZ_CHECK_EQ(process_token.value, static_cast<std::uint64_t>(17));
  DHEPZ_CHECK(host.process_request.operation == modules::ProcessOperation::Capture);
  DHEPZ_CHECK_EQ(host.process_request.arguments[0], std::wstring(L"space value"));

  modules::FolderProbeRequest probe;
  probe.directory = L"C:\\work";
  probe.relative_files = {L"Scripts\\Activate.ps1", L"Scripts\\activate.bat"};
  modules::AsyncRequestToken probe_token;
  DHEPZ_CHECK(host.StartFolderProbe(probe, {}, &probe_token).ok());
  DHEPZ_CHECK_EQ(probe_token.value, static_cast<std::uint64_t>(18));
  DHEPZ_CHECK_EQ(host.folder_request.relative_files.size(), static_cast<std::size_t>(2));

  host.CancelRequest(process_token);
  DHEPZ_CHECK(host.cancelled == process_token);

  json::Value patch = json::Value::Object();
  patch.Set(L"busy", json::Value::Bool(false));
  DHEPZ_CHECK(host.PublishStatePatch(patch).ok());
  const json::Value* busy = host.published_patch.Find(L"busy");
  DHEPZ_CHECK(busy != nullptr);
  DHEPZ_CHECK(!busy->AsBool(true));
}

DHEPZ_TEST(ModuleManifest, MissingRequiredFieldsAreDiagnostics) {
  modules::ModuleManifest manifest;
  std::vector<modules::ManifestDiagnostic> diagnostics;
  DHEPZ_CHECK(!modules::ParseManifest(W(R"({ "tabLabel": "x" })"), &manifest, &diagnostics)
                   .ok());
  DHEPZ_CHECK(!diagnostics.empty());
}

DHEPZ_TEST(ModuleManifest, UnknownCapabilityIsRejected) {
  modules::ModuleManifest manifest;
  std::vector<modules::ManifestDiagnostic> diagnostics;
  DHEPZ_CHECK(!modules::ParseManifest(W(R"({
      "moduleId": "a", "tabLabel": "A", "capabilities": ["kernel:admin"] })"),
                  &manifest, &diagnostics)
                   .ok());
  bool named = false;
  for (const auto& d : diagnostics) {
    if (d.message.find(L"kernel:admin") != std::wstring::npos) named = true;
  }
  DHEPZ_CHECK(named);
}

DHEPZ_TEST(ModuleManifest, UnknownFieldIsRejectedWithLine) {
  modules::ModuleManifest manifest;
  std::vector<modules::ManifestDiagnostic> diagnostics;
  DHEPZ_CHECK(!modules::ParseManifest(W(R"({
  "moduleId": "a",
  "tabLabel": "A",
  "backdoor": true
})"),
                  &manifest, &diagnostics)
                   .ok());
  bool has_line = false;
  for (const auto& d : diagnostics) {
    if (d.line == 4) has_line = true;
  }
  DHEPZ_CHECK(has_line);
}

DHEPZ_TEST(ModuleManifest, MalformedJsonReportsParseDiagnostic) {
  modules::ModuleManifest manifest;
  std::vector<modules::ManifestDiagnostic> diagnostics;
  DHEPZ_CHECK(!modules::ParseManifest(W("{ oops"), &manifest, &diagnostics).ok());
  DHEPZ_CHECK(!diagnostics.empty());
}

DHEPZ_TEST(ModuleContract, DescriptorConsumedThroughHostWithoutSiblings) {
  TestHost host;
  TestModule module;
  DHEPZ_CHECK(module.Bind(host).ok());
  DHEPZ_CHECK(module.host_ == &host);

  json::Value payload;
  json::Value patch;
  DHEPZ_CHECK(module.Handle(L"launch", payload, &patch).ok());
  DHEPZ_CHECK(!module.Handle(L"selfdestruct", payload, &patch).ok());

  DHEPZ_CHECK(host.RequestRoute(L"terminal").ok());
  DHEPZ_CHECK_EQ(host.requested, std::wstring(L"terminal"));

  module.Release();
  DHEPZ_CHECK(module.released);
}

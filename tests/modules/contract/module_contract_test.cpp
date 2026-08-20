#include "modules/contract/module_contract.h"
#include "modules/contract/module_manifest.h"

#include <memory>
#include <string>

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
  core::Status ProcessRun(std::wstring_view, std::wstring*) override { return core::Ok(); }
  void ReportStatus(const core::Status& s) override { reported = !s.ok(); }
  void Log(std::wstring_view, std::wstring_view) override {}
  core::Status RequestRoute(std::wstring_view route) override {
    requested = std::wstring(route);
    return core::Ok();
  }
  std::vector<modules::PeerInfo> Peers() override { return {}; }

  std::wstring last_write;
  std::wstring requested;
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

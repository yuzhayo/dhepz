#include "modules/diagnostics/diagnostics_module.h"

#include "framework/test_case.h"
#include "modules/gate/app_gate.h"
#include "modules/registry/module_registry.h"

#include <cstdio>
#include <memory>
#include <string>

namespace {

std::wstring W(const char* utf8) {
  std::wstring wide;
  for (const char* p = utf8; *p != '\0'; ++p) {
    wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
  }
  return wide;
}

const char* kCore = R"({
  "schema": "dhepz.ui.core",
  "version": 1,
  "tokens": {
    "dark": { "text": "#E9EDF4", "window": "#14161B" },
    "light": { "text": "#181C24", "window": "#F5F6F9" }
  },
  "allows_children": ["screen"],
  "components": {
    "screen": { "properties": {
      "route_id": { "kind": "string", "required": true },
      "module_id": { "kind": "string" },
      "tab_label": { "kind": "string" },
      "show_in_tabs": { "kind": "bool" } } },
    "text": { "properties": { "text": { "kind": "text", "required": true } } }
  }
})";

struct Healthy {
  static std::wstring_view Id() { return L"alpha"; }
  static std::vector<std::wstring> Actions() { return {L"launch"}; }
  static std::vector<std::wstring> Caps() { return {}; }
};
struct Typo {
  static std::wstring_view Id() { return L"typo"; }
  // The handler registered under the misspelled name; module.json declares
  // the correct one, so the gate must reject the module naming the action.
  static std::vector<std::wstring> Actions() { return {L"lauch"}; }
  static std::vector<std::wstring> Caps() { return {}; }
};

template <typename Base>
class SimpleModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return Base::Id(); }
  std::wstring_view TabLabel() const override { return Base::Id(); }
  int Order() const override { return 100; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return Base::Actions(); }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return Base::Caps(); }
  core::Status Bind(modules::ModuleHost&) override { return core::Ok(); }
  core::Status Handle(std::wstring_view, const json::Value&, json::Value*) override {
    return core::Ok();
  }
  void Release() override {}
};

std::unique_ptr<modules::ModuleDescriptor> MakeHealthy() {
  return std::make_unique<SimpleModule<Healthy>>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeTypo() {
  return std::make_unique<SimpleModule<Typo>>();
}

std::wstring ScreenFor(const wchar_t* id) {
  return std::wstring(L"{ \"type\": \"screen\", \"route_id\": \"") + id +
         L"-home\", \"module_id\": \"" + id +
         L"\", \"tab_label\": \"" + id +
         L"\", \"children\": [ { \"type\": \"text\", \"text\": \"x\" } ] }";
}

}  // namespace

DHEPZ_TEST(Diagnostics, ThreeFoldersHealthyActiveBrokenReported) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"alpha", &MakeHealthy);
  modules::RegisterModule(L"typo", &MakeTypo);
  modules::RegisterModule(L"diagnostics", &modules::MakeDiagnosticsForTests);

  const std::wstring screens = ScreenFor(L"alpha") + L", " + ScreenFor(L"typo") + L", " +
                               ScreenFor(L"diagnostics");
  // alpha healthy; typo declares the correct action name in module.json but
  // the code registered the misspelling; malformed manifest does not parse.
  // "broken" fails schema validation (missing tabLabel) — the runtime form
  // of a corrupt manifest; byte-level malformed JSON fails the build (#85).
  const std::wstring manifests =
      L"{ \"moduleId\": \"alpha\", \"tabLabel\": \"alpha\", \"actions\": [\"launch\"] }, "
      L"{ \"moduleId\": \"typo\", \"tabLabel\": \"typo\", \"actions\": [\"launch\"] }, "
      L"{ \"moduleId\": \"broken\" }";
  const std::wstring diagnostics_manifest =
      L"{ \"moduleId\": \"diagnostics\", \"tabLabel\": \"Diagnostics\", "
        L"\"showInTabs\": false, \"actions\": [\"diagnostics:refresh\"] }";
  const std::wstring embedded = L"{ \"core\": " + W(kCore) + L", \"components\": [ " +
                                screens + L" ], \"modules\": [ " + manifests + L", " +
                                diagnostics_manifest + L" ] }";

  modules::AppGate gate;
  DHEPZ_CHECK(gate.StartWithEmbedded(embedded).ok());

  DHEPZ_CHECK(gate.Mounted(L"alpha"));
  DHEPZ_CHECK(gate.Mounted(L"diagnostics"));
  DHEPZ_CHECK(!gate.Mounted(L"typo"));

  // Both broken entries reported: the typo'd action and the malformed manifest.
  DHEPZ_CHECK_EQ(gate.Rejects().size(), static_cast<std::size_t>(2));
  bool saw_typo = false;
  bool saw_malformed = false;
  for (const modules::RejectEntry& reject : gate.Rejects()) {
    if (reject.module_id == L"typo" &&
        reject.reason.find(L"launch") != std::wstring::npos) {
      saw_typo = true;
    }
    if (reject.module_id == L"broken" &&
        reject.reason.find(L"manifest invalid") != std::wstring::npos) {
      saw_malformed = true;
    }
  }
  DHEPZ_CHECK(saw_typo);
  DHEPZ_CHECK(saw_malformed);

  // The diagnostics module summarizes the reject list on refresh.
  DHEPZ_CHECK(gate.Activate(L"diagnostics").ok());
  json::Value payload;
  json::Value patch;
  DHEPZ_CHECK(gate.Dispatch(L"diagnostics:refresh", payload, &patch).ok());
  DHEPZ_CHECK(modules::DiagnosticsLastSummary().find(L"typo") != std::wstring::npos);
  modules::ResetRegistryForTests();
}

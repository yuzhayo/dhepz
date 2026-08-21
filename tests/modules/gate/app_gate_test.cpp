#include "modules/gate/app_gate.h"

#include <memory>
#include <string>

#include "framework/test_case.h"
#include "modules/registry/module_registry.h"

namespace {

std::wstring W(const char* utf8) {
  std::wstring wide;
  for (const char* p = utf8; *p != '\0'; ++p) {
    wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
  }
  return wide;
}

template <typename Base>
class SimpleModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return Base::Id(); }
  std::wstring_view TabLabel() const override { return Base::Id(); }
  int Order() const override { return 100; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return Base::Actions(); }
  std::vector<std::wstring> DeclaredBindings() const override { return Base::Bindings(); }
  std::vector<std::wstring> DeclaredCapabilities() const override { return Base::Caps(); }
  core::Status Bind(modules::ModuleHost& host) override {
    bound_ = true;
    (void)host;
    return core::Ok();
  }
  core::Status Handle(std::wstring_view action, const json::Value&, json::Value*) override {
    handled_ = std::wstring(action);
    return core::Ok();
  }
  void Release() override {}
  bool bound_ = false;
  std::wstring handled_;
};

struct Alpha {
  static std::wstring_view Id() { return L"alpha"; }
  static std::vector<std::wstring> Actions() { return {L"alpha-launch"}; }
  static std::vector<std::wstring> Caps() { return {}; }
  static std::vector<std::wstring> Bindings() { return {}; }
};
struct BadCap {
  static std::wstring_view Id() { return L"badcap"; }
  static std::vector<std::wstring> Actions() { return {}; }
  static std::vector<std::wstring> Caps() { return {L"kernel:admin"}; }
  static std::vector<std::wstring> Bindings() { return {}; }
};
struct Mismatch {
  static std::wstring_view Id() { return L"mismatch"; }
  static std::vector<std::wstring> Actions() { return {}; }
  static std::vector<std::wstring> Caps() { return {L"config:write"}; }
  static std::vector<std::wstring> Bindings() { return {}; }
};
struct ClaimA {
  static std::wstring_view Id() { return L"settings"; }
  static std::vector<std::wstring> Actions() { return {}; }
  static std::vector<std::wstring> Caps() { return {L"settings:all"}; }
  static std::vector<std::wstring> Bindings() { return {}; }
};
struct RefModule {
  static std::wstring_view Id() { return L"ref-module"; }
  static std::vector<std::wstring> Actions() { return {}; }
  static std::vector<std::wstring> Caps() { return {}; }
  static std::vector<std::wstring> Bindings() { return {}; }
};
struct DuplicateAction {
  static std::wstring_view Id() { return L"duplicate"; }
  static std::vector<std::wstring> Actions() { return {L"alpha-launch"}; }
  static std::vector<std::wstring> Caps() { return {}; }
  static std::vector<std::wstring> Bindings() { return {}; }
};

class RouteOwnerModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"route-owner"; }
  std::wstring_view TabLabel() const override { return L"route-owner"; }
  int Order() const override { return 100; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return L"owner-settings"; }
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

std::unique_ptr<modules::ModuleDescriptor> MakeAlpha() {
  return std::make_unique<SimpleModule<Alpha>>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeBadCap() {
  return std::make_unique<SimpleModule<BadCap>>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeMismatch() {
  return std::make_unique<SimpleModule<Mismatch>>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeClaimA() {
  return std::make_unique<SimpleModule<ClaimA>>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeRefModule() {
  return std::make_unique<SimpleModule<RefModule>>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeDuplicateAction() {
  return std::make_unique<SimpleModule<DuplicateAction>>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeRouteOwner() {
  return std::make_unique<RouteOwnerModule>();
}
const char* kCore = R"({
  "schema": "dhepz.ui.core",
  "version": 1,
  "tokens": {
    "dark": { "text": "#E9EDF4", "window": "#14161B" },
    "light": { "text": "#181C24", "window": "#F5F6F9" }
  },
  "common": { "properties": { "style": { "kind": "string" } } },
  "allows_children": ["screen"],
  "components": {
    "screen": { "properties": {
      "route_id": { "kind": "string", "required": true },
      "module_id": { "kind": "string" },
      "tab_label": { "kind": "string" } } },
    "text": { "properties": { "text": { "kind": "text", "required": true } } },
    "button": { "properties": {
      "label": { "kind": "text", "required": true },
      "action": { "kind": "string" },
      "action_payload": { "kind": "object" } } },
    "input": { "properties": { "value_binding": { "kind": "binding" } } }
  }
})";

const char* kScreenAlpha =
    R"({ "type": "screen", "route_id": "alpha-home", "module_id": "alpha",
         "tab_label": "alpha", "children": [ { "type": "text", "text": "a" } ] })";

std::wstring EmbeddedWith(const std::wstring& screens, const std::wstring& manifests) {
  return L"{ \"core\": " + W(kCore) + L", \"components\": [ " + screens +
         L" ], \"modules\": [ " + manifests + L" ] }";
}

std::wstring ManifestFor(const wchar_t* id, const wchar_t* caps = L"",
                         const wchar_t* actions = L"") {
  return std::wstring(L"{ \"moduleId\": \"") + id + L"\", \"tabLabel\": \"" + id +
         L"\", \"actions\": [" + actions + L"], \"capabilities\": [" + caps +
         L"] }";
}

}  // namespace

DHEPZ_TEST(AppGate, HealthyModuleMountsActivatesAndDispatches) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"alpha", &MakeAlpha);
  modules::AppGate gate;
  DHEPZ_CHECK(
      gate.StartWithEmbedded(EmbeddedWith(
          W(kScreenAlpha), ManifestFor(L"alpha", L"", L"\"alpha-launch\"")))
          .ok());
  DHEPZ_CHECK(gate.Rejects().empty());
  DHEPZ_CHECK(gate.Mounted(L"alpha"));
  DHEPZ_CHECK_EQ(gate.Peers().size(), static_cast<std::size_t>(1));

  DHEPZ_CHECK(gate.Activate(L"alpha-home").ok());
  json::Value payload;
  json::Value patch;
  DHEPZ_CHECK(gate.Dispatch(L"alpha-launch", payload, &patch).ok());
  DHEPZ_CHECK(!gate.Dispatch(L"unknown-action", payload, &patch).ok());

  // RequestRoute navigates and activates lazily.
  DHEPZ_CHECK(gate.RequestRoute(L"alpha-home").ok());
  DHEPZ_CHECK_EQ(gate.current_route(), std::wstring(L"alpha-home"));
  DHEPZ_CHECK(!gate.RequestRoute(L"nope").ok());
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGate, BrokenModulesRejectedAppContinues) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"alpha", &MakeAlpha);
  modules::RegisterModule(L"badcap", &MakeBadCap);
  modules::RegisterModule(L"ghost", &MakeAlpha);  // registered but no screen half
  modules::AppGate gate;
  const std::wstring screens =
      W(kScreenAlpha) +
      L", { \"type\": \"screen\", \"route_id\": \"badcap-home\", \"module_id\": \"badcap\","
        L" \"children\": [ { \"type\": \"text\", \"text\": \"b\" } ] }";
  const std::wstring manifests =
      ManifestFor(L"alpha", L"", L"\"alpha-launch\"") + L", " +
      ManifestFor(L"badcap", L"\"kernel:admin\"") + L", " +
      ManifestFor(L"ghost");
  DHEPZ_CHECK(gate.StartWithEmbedded(EmbeddedWith(screens, manifests)).ok());

  DHEPZ_CHECK(gate.Mounted(L"alpha"));
  DHEPZ_CHECK(!gate.Mounted(L"badcap"));
  DHEPZ_CHECK(!gate.Mounted(L"ghost"));
  DHEPZ_CHECK_EQ(gate.Rejects().size(), static_cast<std::size_t>(2));
  bool saw_unknown_cap = false;
  bool saw_no_screen = false;
  for (const modules::RejectEntry& reject : gate.Rejects()) {
    if (reject.reason.find(L"kernel:admin") != std::wstring::npos) saw_unknown_cap = true;
    if (reject.reason.find(L"screen half") != std::wstring::npos) saw_no_screen = true;
  }
  DHEPZ_CHECK(saw_unknown_cap);
  DHEPZ_CHECK(saw_no_screen);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGate, CapabilityMismatchBetweenCodeAndManifestRejected) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"mismatch", &MakeMismatch);
  modules::AppGate gate;
  const std::wstring screens =
      L"{ \"type\": \"screen\", \"route_id\": \"mismatch-home\", \"module_id\": \"mismatch\","
        L" \"children\": [ { \"type\": \"text\", \"text\": \"m\" } ] }";
  // Manifest declares no capabilities; the code declares config:write.
  DHEPZ_CHECK(gate.StartWithEmbedded(EmbeddedWith(screens, ManifestFor(L"mismatch"))).ok());
  DHEPZ_CHECK(!gate.Mounted(L"mismatch"));
  DHEPZ_CHECK(gate.document()->FindRoute(L"mismatch-home") == nullptr);
  DHEPZ_CHECK_EQ(gate.Rejects().size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK(gate.Rejects()[0].reason.find(L"does not match") != std::wstring::npos);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGateContract, ExtraCppActionLeavesNoRouteOrDispatch) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"alpha", &MakeAlpha);
  modules::AppGate gate;
  DHEPZ_CHECK(gate.StartWithEmbedded(
                      EmbeddedWith(W(kScreenAlpha), ManifestFor(L"alpha")))
                  .ok());
  DHEPZ_CHECK(!gate.Mounted(L"alpha"));
  DHEPZ_CHECK(gate.document()->FindRoute(L"alpha-home") == nullptr);
  json::Value payload;
  json::Value patch;
  DHEPZ_CHECK(!gate.Dispatch(L"alpha-launch", payload, &patch).ok());
  DHEPZ_CHECK(gate.Rejects()[0].reason.find(L"alpha-launch") !=
              std::wstring::npos);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGateContract, MetadataDriftNamesScreenLocation) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"alpha", &MakeAlpha);
  const std::wstring screen =
      L"{ \"type\": \"screen\", \"route_id\": \"alpha-home\", "
      L"\"module_id\": \"alpha\",\n  \"tab_label\": \"Wrong\" }";
  modules::AppGate gate;
  DHEPZ_CHECK(gate.StartWithEmbedded(EmbeddedWith(
                      screen, ManifestFor(L"alpha", L"", L"\"alpha-launch\"")))
                  .ok());
  DHEPZ_CHECK(!gate.Mounted(L"alpha"));
  DHEPZ_CHECK(gate.Rejects()[0].reason.find(L"screen metadata") !=
              std::wstring::npos);
  DHEPZ_CHECK(!gate.Rejects()[0].file.empty());
  DHEPZ_CHECK(gate.Rejects()[0].line > 0);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGateContract, UndeclaredScreenReferencesAreRejected) {
  const std::wstring screens[] = {
      L"{ \"type\": \"screen\", \"route_id\": \"ref-home\", "
      L"\"module_id\": \"ref-module\", \"children\": ["
      L"{ \"type\": \"button\", \"label\": \"x\", \"action\": \"missing\" } ] }",
      L"{ \"type\": \"screen\", \"route_id\": \"ref-home\", "
      L"\"module_id\": \"ref-module\", \"children\": ["
      L"{ \"type\": \"input\", \"value_binding\": \"missing\" } ] }",
      L"{ \"type\": \"screen\", \"route_id\": \"ref-home\", "
      L"\"module_id\": \"ref-module\", \"style\": \"missing\" }"};
  const wchar_t* reasons[] = {L"undeclared action", L"undeclared binding",
                              L"unknown style"};
  for (int index = 0; index < 3; ++index) {
    modules::ResetRegistryForTests();
    modules::RegisterModule(L"ref-module", &MakeRefModule);
    modules::AppGate gate;
    DHEPZ_CHECK(gate.StartWithEmbedded(
                        EmbeddedWith(screens[index], ManifestFor(L"ref-module")))
                    .ok());
    DHEPZ_CHECK(!gate.Mounted(L"ref-module"));
    DHEPZ_CHECK(gate.Rejects()[0].reason.find(reasons[index]) !=
                std::wstring::npos);
    DHEPZ_CHECK(gate.Rejects()[0].line > 0);
  }
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGateContract, DuplicateActionRejectsSecondOwnerAndItsRoute) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"alpha", &MakeAlpha);
  modules::RegisterModule(L"duplicate", &MakeDuplicateAction);
  const std::wstring duplicate_screen =
      L"{ \"type\": \"screen\", \"route_id\": \"duplicate-home\", "
      L"\"module_id\": \"duplicate\", \"tab_label\": \"duplicate\" }";
  modules::AppGate gate;
  DHEPZ_CHECK(gate.StartWithEmbedded(EmbeddedWith(
                      W(kScreenAlpha) + L", " + duplicate_screen,
                      ManifestFor(L"alpha", L"", L"\"alpha-launch\"") + L", " +
                          ManifestFor(L"duplicate", L"", L"\"alpha-launch\"")))
                  .ok());
  DHEPZ_CHECK(gate.Mounted(L"alpha"));
  DHEPZ_CHECK(!gate.Mounted(L"duplicate"));
  DHEPZ_CHECK(gate.document()->FindRoute(L"duplicate-home") == nullptr);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGateContract, GeneratedEnvelopePreservesFileLineAndColumn) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"ref-module", &MakeRefModule);
  const std::wstring screen =
      L"{\n  \"components\": [\n    { \"type\": \"screen\", "
      L"\"route_id\": \"ref-home\",\n      \"module_id\": \"ref-module\", "
      L"\"children\": [\n        { \"type\": \"button\", \"label\": \"x\", "
      L"\"action\": \"missing\" }\n      ] }\n  ]\n}";
  const std::wstring manifest =
      L"{\n  \"moduleId\": \"ref-module\",\n  \"tabLabel\": \"ref-module\",\n"
      L"  \"actions\": []\n}";
  json::Value source_entry = json::Value::Object();
  source_entry.Set(L"file", json::Value::String(
                                L"src/modules/ref-module/screen.json"));
  source_entry.Set(L"text", json::Value::String(screen));
  json::Value sources = json::Value::Array();
  sources.Append(std::move(source_entry));
  json::Value manifest_entry = json::Value::Object();
  manifest_entry.Set(L"file", json::Value::String(
                                  L"src/modules/ref-module/module.json"));
  manifest_entry.Set(L"text", json::Value::String(manifest));
  json::Value manifests = json::Value::Array();
  manifests.Append(std::move(manifest_entry));
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  json::Value envelope = json::Value::Object();
  envelope.Set(L"core", std::move(core));
  envelope.Set(L"sources", std::move(sources));
  envelope.Set(L"modules", std::move(manifests));

  modules::AppGate gate;
  DHEPZ_CHECK(gate.StartWithEmbedded(json::Serialize(envelope)).ok());
  DHEPZ_CHECK_EQ(gate.Rejects()[0].file,
                 std::wstring(L"src/modules/ref-module/screen.json"));
  DHEPZ_CHECK_EQ(gate.Rejects()[0].line, 5);
  DHEPZ_CHECK(gate.Rejects()[0].column > 0);
  DHEPZ_CHECK(gate.Rejects()[0].stage ==
              modules::DiagnosticStage::Pairing);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGateContract, UnknownComponentQuarantinesOnlyItsModuleSource) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"ref-module", &MakeRefModule);
  const std::wstring screen =
      L"{ \"components\": [ { \"type\": \"screen\", "
      L"\"route_id\": \"ref-home\", \"module_id\": \"ref-module\", "
      L"\"children\": [ { \"type\": \"unknown-widget\" } ] } ] }";
  const std::wstring manifest =
      L"{ \"moduleId\": \"ref-module\", \"tabLabel\": \"ref-module\" }";
  json::Value source_entry = json::Value::Object();
  source_entry.Set(L"file", json::Value::String(L"ref/screen.json"));
  source_entry.Set(L"text", json::Value::String(screen));
  json::Value sources = json::Value::Array();
  sources.Append(std::move(source_entry));
  json::Value manifest_entry = json::Value::Object();
  manifest_entry.Set(L"file", json::Value::String(L"ref/module.json"));
  manifest_entry.Set(L"text", json::Value::String(manifest));
  json::Value manifests = json::Value::Array();
  manifests.Append(std::move(manifest_entry));
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  json::Value envelope = json::Value::Object();
  envelope.Set(L"core", std::move(core));
  envelope.Set(L"sources", std::move(sources));
  envelope.Set(L"modules", std::move(manifests));

  modules::AppGate gate;
  DHEPZ_CHECK(gate.StartWithEmbedded(json::Serialize(envelope)).ok());
  DHEPZ_CHECK(!gate.Mounted(L"ref-module"));
  DHEPZ_CHECK(gate.document()->FindRoute(L"ref-home") == nullptr);
  DHEPZ_CHECK_EQ(gate.Rejects()[0].file,
                 std::wstring(L"ref/screen.json"));
  DHEPZ_CHECK(gate.Rejects()[0].reason.find(L"unknown") !=
              std::wstring::npos);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGateContract, SettingsRouteMustBelongToDeclaringModule) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"route-owner", &MakeRouteOwner);
  const std::wstring screens =
      L"{ \"type\": \"screen\", \"route_id\": \"owner-home\", "
      L"\"module_id\": \"route-owner\", \"tab_label\": \"route-owner\" }, "
      L"{ \"type\": \"screen\", \"route_id\": \"owner-settings\", "
      L"\"module_id\": \"other\" }";
  const std::wstring manifest =
      L"{ \"moduleId\": \"route-owner\", \"tabLabel\": \"route-owner\", "
      L"\"settingsRoute\": \"owner-settings\" }";
  modules::AppGate gate;
  DHEPZ_CHECK(
      gate.StartWithEmbedded(EmbeddedWith(screens, manifest)).ok());
  DHEPZ_CHECK(!gate.Mounted(L"route-owner"));
  bool saw_owner_reject = false;
  for (const modules::RejectEntry& reject : gate.Rejects()) {
    if (reject.module_id == L"route-owner" &&
        reject.reason.find(L"settingsRoute") != std::wstring::npos) {
      saw_owner_reject = true;
    }
  }
  DHEPZ_CHECK(saw_owner_reject);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGate, SettingsAllIsSingleClaimant) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"settings", &MakeClaimA);
  modules::AppGate gate;
  const std::wstring screens =
      L"{ \"type\": \"screen\", \"route_id\": \"settings-home\", \"module_id\": \"settings\","
        L" \"children\": [ { \"type\": \"text\", \"text\": \"a\" } ] }";
  const std::wstring manifests =
      ManifestFor(L"settings", L"\"settings:all\"") + L", " +
      ManifestFor(L"settings", L"\"settings:all\"");
  DHEPZ_CHECK(gate.StartWithEmbedded(EmbeddedWith(screens, manifests)).ok());
  DHEPZ_CHECK(gate.Mounted(L"settings"));
  DHEPZ_CHECK_EQ(gate.Rejects().size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK(gate.Rejects()[0].reason.find(L"already claimed") != std::wstring::npos);
  DHEPZ_CHECK(!gate.Rejects()[0].file.empty());
  DHEPZ_CHECK(gate.Rejects()[0].line > 0);
  modules::ResetRegistryForTests();
}

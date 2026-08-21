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
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return Base::Caps(); }
  core::Status Bind(modules::ModuleHost& host) override {
    bound_ = true;
    return host.SettingsWrite(L"k", L"v");
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
};
struct BadCap {
  static std::wstring_view Id() { return L"badcap"; }
  static std::vector<std::wstring> Actions() { return {}; }
  static std::vector<std::wstring> Caps() { return {L"kernel:admin"}; }
};
struct Mismatch {
  static std::wstring_view Id() { return L"mismatch"; }
  static std::vector<std::wstring> Actions() { return {}; }
  static std::vector<std::wstring> Caps() { return {L"config:write"}; }
};
struct ClaimA {
  static std::wstring_view Id() { return L"claim-a"; }
  static std::vector<std::wstring> Actions() { return {}; }
  static std::vector<std::wstring> Caps() { return {L"settings:all"}; }
};
struct ClaimB {
  static std::wstring_view Id() { return L"claim-b"; }
  static std::vector<std::wstring> Actions() { return {}; }
  static std::vector<std::wstring> Caps() { return {L"settings:all"}; }
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
std::unique_ptr<modules::ModuleDescriptor> MakeClaimB() {
  return std::make_unique<SimpleModule<ClaimB>>();
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
      "tab_label": { "kind": "string" } } },
    "text": { "properties": { "text": { "kind": "text", "required": true } } }
  }
})";

const char* kScreenAlpha =
    R"({ "type": "screen", "route_id": "alpha-home", "module_id": "alpha",
         "tab_label": "Alpha", "children": [ { "type": "text", "text": "a" } ] })";

std::wstring EmbeddedWith(const std::wstring& screens, const std::wstring& manifests) {
  return L"{ \"core\": " + W(kCore) + L", \"components\": [ " + screens +
         L" ], \"modules\": [ " + manifests + L" ] }";
}

std::wstring ManifestFor(const wchar_t* id, const wchar_t* caps = L"") {
  return std::wstring(L"{ \"moduleId\": \"") + id + L"\", \"tabLabel\": \"" + id +
         L"\", \"capabilities\": [" + caps + L"] }";
}

}  // namespace

DHEPZ_TEST(AppGate, HealthyModuleMountsActivatesAndDispatches) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"alpha", &MakeAlpha);
  modules::AppGate gate;
  DHEPZ_CHECK(
      gate.StartWithEmbedded(EmbeddedWith(W(kScreenAlpha), ManifestFor(L"alpha"))).ok());
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
      ManifestFor(L"alpha") + L", " + ManifestFor(L"badcap", L"\"kernel:admin\"") + L", " +
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
  DHEPZ_CHECK_EQ(gate.Rejects().size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK(gate.Rejects()[0].reason.find(L"does not match") != std::wstring::npos);
  modules::ResetRegistryForTests();
}

DHEPZ_TEST(AppGate, SettingsAllIsSingleClaimant) {
  modules::ResetRegistryForTests();
  modules::RegisterModule(L"claim-a", &MakeClaimA);
  modules::RegisterModule(L"claim-b", &MakeClaimB);
  modules::AppGate gate;
  const std::wstring screens =
      L"{ \"type\": \"screen\", \"route_id\": \"a-home\", \"module_id\": \"claim-a\","
        L" \"children\": [ { \"type\": \"text\", \"text\": \"a\" } ] },"
        L"{ \"type\": \"screen\", \"route_id\": \"b-home\", \"module_id\": \"claim-b\","
        L" \"children\": [ { \"type\": \"text\", \"text\": \"b\" } ] }";
  const std::wstring manifests =
      ManifestFor(L"claim-a", L"\"settings:all\"") + L", " +
      ManifestFor(L"claim-b", L"\"settings:all\"");
  DHEPZ_CHECK(gate.StartWithEmbedded(EmbeddedWith(screens, manifests)).ok());
  DHEPZ_CHECK(gate.Mounted(L"claim-a"));
  DHEPZ_CHECK(!gate.Mounted(L"claim-b"));
  DHEPZ_CHECK_EQ(gate.Rejects().size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK(gate.Rejects()[0].reason.find(L"already claimed") != std::wstring::npos);
  modules::ResetRegistryForTests();
}

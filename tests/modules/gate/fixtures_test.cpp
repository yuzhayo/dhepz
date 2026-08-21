#include "modules/gate/app_gate.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/json.h"
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

struct Healthy {
  static std::wstring_view Id() { return L"fixture-healthy"; }
  static std::wstring_view Label() { return L"Fixture Healthy"; }
  static int Order() { return 500; }
  static std::vector<std::wstring> Actions() { return {L"fixture-launch"}; }
};
struct Typo {
  static std::wstring_view Id() { return L"fixture-typo"; }
  static std::wstring_view Label() { return L"Fixture Typo"; }
  static int Order() { return 501; }
  // The misspelling: module.json declares fixture-launch, the handler
  // registers under this name, so the gate must reject the module.
  static std::vector<std::wstring> Actions() { return {L"fixture-launhc"}; }
};

template <typename Base>
class FixtureModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return Base::Id(); }
  std::wstring_view TabLabel() const override { return Base::Label(); }
  int Order() const override { return Base::Order(); }
  bool ShowInTabs() const override { return false; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return Base::Actions(); }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }
  core::Status Bind(modules::ModuleHost&) override { return core::Ok(); }
  core::Status Handle(std::wstring_view, const json::Value&, json::Value*) override {
    return core::Ok();
  }
  void Release() override {}
};

std::unique_ptr<modules::ModuleDescriptor> MakeHealthy() {
  return std::make_unique<FixtureModule<Healthy>>();
}
std::unique_ptr<modules::ModuleDescriptor> MakeTypo() {
  return std::make_unique<FixtureModule<Typo>>();
}

}  // namespace

// The three fixture folders (healthy / typo'd action / malformed manifest)
// drive the gate the way CI does: healthy mounts, broken ones are reported,
// the app still starts, and the reject list is exported for the CI artifact.
DHEPZ_TEST(ContractFixtures, DegradedModeAcrossThreeFolders) {
  const std::wstring root = RepoRoot();
  const std::wstring fixtures = root + L"\\tests\\fixtures\\modules";

  json::Value core;
  DHEPZ_CHECK(json::Parse(W(ReadFile(root + L"\\assets\\ui\\core.json").c_str()), &core).ok());

  std::wstring screens;
  std::wstring manifests;
  int broken_manifests = 0;
  const wchar_t* names[3] = {L"fixture-broken", L"fixture-healthy", L"fixture-typo"};
  for (const wchar_t* name : names) {
    const std::string manifest_text =
        ReadFile(fixtures + L"\\" + name + L"\\module.json");
    json::Value manifest_value;
    if (!json::Parse(Ws(manifest_text), &manifest_value).ok()) {
      ++broken_manifests;  // byte-level malformed: fails the build merge, not embedded
      continue;
    }
    if (!manifests.empty()) manifests += L", ";
    manifests += Ws(manifest_text);
    const std::string screen_text =
        ReadFile(fixtures + L"\\" + name + L"\\screen.json");
    json::Value screen_doc;
    if (json::Parse(Ws(screen_text), &screen_doc).ok()) {
      const json::Value* components = screen_doc.Find(L"components");
      if (components != nullptr) {
        for (const json::Value& component : components->items()) {
          if (!screens.empty()) screens += L", ";
          screens += json::Serialize(component);
        }
      }
    }
  }
  DHEPZ_CHECK_EQ(broken_manifests, 1);

  modules::ResetRegistryForTests();
  modules::RegisterModule(L"fixture-healthy", &MakeHealthy);
  modules::RegisterModule(L"fixture-typo", &MakeTypo);

  const std::wstring embedded = L"{ \"core\": " + json::Serialize(core) +
                                L", \"components\": [ " + screens + L" ], \"modules\": [ " +
                                manifests + L" ] }";
  modules::AppGate gate;
  DHEPZ_CHECK(gate.StartWithEmbedded(embedded).ok());

  DHEPZ_CHECK(gate.Mounted(L"fixture-healthy"));
  DHEPZ_CHECK(!gate.Mounted(L"fixture-typo"));

  bool saw_typo = false;
  for (const modules::RejectEntry& reject : gate.Rejects()) {
    if (reject.module_id == L"fixture-typo") saw_typo = true;
  }
  DHEPZ_CHECK(saw_typo);

  // CI-readable diagnostics artifact.
  FILE* out = nullptr;
  fopen_s(&out, "artifacts\\diagnostics.json", "w");
  DHEPZ_CHECK(out != nullptr);
  fprintf(out, "{ \"rejects\": [");
  bool first = true;
  for (const modules::RejectEntry& reject : gate.Rejects()) {
    std::string id;
    std::string reason;
    for (const wchar_t c : reject.module_id) id.push_back(static_cast<char>(c));
    for (const wchar_t c : reject.reason) reason.push_back(static_cast<char>(c));
    fprintf(out, "%s{ \"moduleId\": \"%s\", \"reason\": \"%s\" }", first ? "" : ", ",
            id.c_str(), reason.c_str());
    first = false;
  }
  fprintf(out, " ] }\n");
  fclose(out);
  modules::ResetRegistryForTests();
}

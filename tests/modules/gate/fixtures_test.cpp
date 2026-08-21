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
struct Metadata {
  static std::wstring_view Id() { return L"fixture-metadata"; }
  // Deliberately disagrees with the valid embedded manifest.
  static std::wstring_view Label() { return L"Wrong Metadata"; }
  static int Order() { return 502; }
  static std::vector<std::wstring> Actions() { return {}; }
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
std::unique_ptr<modules::ModuleDescriptor> MakeMetadata() {
  return std::make_unique<FixtureModule<Metadata>>();
}

}  // namespace

// Runtime composition consumes compiled RCDATA containing one healthy and two
// syntactically valid contract-broken modules. Byte-level malformed JSON is a
// separate build-stage failure and is checked below plus in CI.
DHEPZ_TEST(ContractFixtures, DegradedModeAcrossThreeFolders) {
  const std::wstring root = RepoRoot();
  const std::wstring fixtures = root + L"\\tests\\fixtures\\modules";
  json::Value malformed;
  DHEPZ_CHECK_FALSE(json::Parse(
      Ws(ReadFile(fixtures + L"\\fixture-broken\\module.json")),
      &malformed).ok());

  modules::ResetRegistryForTests();
  modules::RegisterModule(L"fixture-healthy", &MakeHealthy);
  modules::RegisterModule(L"fixture-typo", &MakeTypo);
  modules::RegisterModule(L"fixture-metadata", &MakeMetadata);

  modules::AppGate gate;
  DHEPZ_CHECK(gate.StartFromResource(L"APP_GATE_CHECKPOINT_UI").ok());

  DHEPZ_CHECK(gate.Mounted(L"fixture-healthy"));
  DHEPZ_CHECK(!gate.Mounted(L"fixture-typo"));
  DHEPZ_CHECK(!gate.Mounted(L"fixture-metadata"));
  json::Value patch = json::Value::Object();
  DHEPZ_CHECK(gate.Dispatch(L"fixture-launch", json::Value::Object(), &patch).ok());

  bool saw_typo = false;
  bool saw_metadata = false;
  for (const modules::RejectEntry& reject : gate.Rejects()) {
    if (reject.module_id == L"fixture-typo") {
      saw_typo = true;
      DHEPZ_CHECK_EQ(reject.file, std::wstring(L"fixtures/typo/module.json"));
      DHEPZ_CHECK(reject.line > 0 && reject.column > 0);
    }
    if (reject.module_id == L"fixture-metadata") {
      saw_metadata = true;
      DHEPZ_CHECK_EQ(reject.file, std::wstring(L"fixtures/metadata/module.json"));
      DHEPZ_CHECK(reject.line > 0 && reject.column > 0);
    }
  }
  DHEPZ_CHECK(saw_typo);
  DHEPZ_CHECK(saw_metadata);

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

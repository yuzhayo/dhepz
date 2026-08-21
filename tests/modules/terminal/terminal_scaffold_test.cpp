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

DHEPZ_TEST(TerminalLogic, WslCommandLineQuotesTheDistro) {
  terminal::LaunchSpec spec;
  spec.shell = terminal::Shell::Wsl;
  spec.wsl_distro = L"My Distro";
  const std::wstring command = terminal::BuildCommandLine(spec);
  DHEPZ_CHECK(command.find(L"\"My Distro\"") != std::wstring::npos);
}

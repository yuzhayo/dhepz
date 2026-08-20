#include "ui/config/config_store.h"

#include <windows.h>

#include <fstream>
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

void WriteFile(const std::wstring& path, const std::string& utf8) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << utf8;
}

bool Exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

const char* kCore = R"({
  "schema": "dhepz.ui.core",
  "version": 1,
  "tokens": { "dark": { "text": "#FFFFFF" }, "light": { "text": "#000000" } },
  "allows_children": ["screen"],
  "components": {
    "screen": { "properties": { "route_id": { "kind": "string", "required": true },
                                "tab_label": { "kind": "string" } } },
    "text": { "properties": { "text": { "kind": "text", "required": true } } }
  }
})";

const char* kEmbedded = R"({
  "components": [
    { "type": "screen", "route_id": "home", "tab_label": "Embedded", "children": [
      { "type": "text", "text": "e" } ] }
  ]
})";

const char* kOverride = R"({
  "components": [
    { "type": "screen", "route_id": "home", "tab_label": "Override", "children": [
      { "type": "text", "text": "o" } ] }
  ]
})";

const char* kOverride2 = R"({
  "components": [
    { "type": "screen", "route_id": "home", "tab_label": "Fixed", "children": [
      { "type": "text", "text": "f" } ] }
  ]
})";

std::wstring TempOverridePath() {
  wchar_t buffer[MAX_PATH]{};
  GetTempPathW(MAX_PATH, buffer);
  std::wstring dir = std::wstring(buffer) + L"dhepz-q-" +
                     std::to_wstring(GetCurrentProcessId());
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\override.json";
}

ui::config::ConfigStore MakeStore(const std::wstring& override_path) {
  return ui::config::ConfigStore(W(kCore), {{L"embedded", W(kEmbedded)}}, override_path);
}

}  // namespace

DHEPZ_TEST(ConfigStore, ValidOverrideAtStartupMerges) {
  const std::wstring path = TempOverridePath();
  WriteFile(path, kOverride);
  ui::config::ConfigStore store = MakeStore(path);
  DHEPZ_CHECK(store.Load().ok());
  DHEPZ_CHECK(store.document() != nullptr);
  DHEPZ_CHECK_EQ(store.document()->FindRoute(L"home")->tab_label, std::wstring(L"Override"));
}

DHEPZ_TEST(ConfigStore, CorruptAtBootstrapFallsBackAndQuarantines) {
  const std::wstring path = TempOverridePath();
  WriteFile(path, "{ this is not json");
  ui::config::ConfigStore store = MakeStore(path);
  DHEPZ_CHECK(store.Load().ok());
  DHEPZ_CHECK_EQ(store.document()->FindRoute(L"home")->tab_label, std::wstring(L"Embedded"));
  DHEPZ_CHECK(!store.diagnostics().empty());
  DHEPZ_CHECK_FALSE(Exists(path));             // moved aside
  DHEPZ_CHECK(Exists(path + L".quarantine"));  // preserved for the user
}

DHEPZ_TEST(ConfigStore, TruncatedOverrideIsCorruptNotCrash) {
  const std::wstring path = TempOverridePath();
  const std::string full(kOverride);
  WriteFile(path, full.substr(0, full.size() / 2));  // a partial write
  ui::config::ConfigStore store = MakeStore(path);
  DHEPZ_CHECK(store.Load().ok());
  DHEPZ_CHECK_EQ(store.document()->FindRoute(L"home")->tab_label, std::wstring(L"Embedded"));
}

DHEPZ_TEST(ConfigStore, CorruptDuringReloadKeepsTheLiveDocument) {
  const std::wstring path = TempOverridePath();
  WriteFile(path, kOverride);
  ui::config::ConfigStore store = MakeStore(path);
  DHEPZ_CHECK(store.Load().ok());
  DHEPZ_CHECK_EQ(store.document()->FindRoute(L"home")->tab_label, std::wstring(L"Override"));

  WriteFile(path, "{ truncated");
  const std::size_t before = store.diagnostics().size();
  DHEPZ_CHECK(!store.Reload().ok());
  // Live document untouched, diagnostic recorded, file NOT quarantined so
  // the user can fix it in place.
  DHEPZ_CHECK_EQ(store.document()->FindRoute(L"home")->tab_label, std::wstring(L"Override"));
  DHEPZ_CHECK(store.diagnostics().size() > before);
  DHEPZ_CHECK(Exists(path));
}

DHEPZ_TEST(ConfigStore, RecoveryOnNextReloadWithValidOverride) {
  const std::wstring path = TempOverridePath();
  WriteFile(path, "{ broken");
  ui::config::ConfigStore store = MakeStore(path);
  DHEPZ_CHECK(store.Load().ok());
  DHEPZ_CHECK_EQ(store.document()->FindRoute(L"home")->tab_label, std::wstring(L"Embedded"));

  WriteFile(path, kOverride2);
  DHEPZ_CHECK(store.Reload().ok());
  DHEPZ_CHECK_EQ(store.document()->FindRoute(L"home")->tab_label, std::wstring(L"Fixed"));
}

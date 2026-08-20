#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/json.h"
#include "framework/test_case.h"
#include "ui/config/resolved_ui_document.h"

namespace {

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

std::string ReadFile(const std::wstring& path) {
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream text;
  text << stream.rdbuf();
  return text.str();
}

}  // namespace

DHEPZ_TEST(RepoUiConfig, EmbeddedScreensResolveClean) {
  const std::wstring root = RepoRoot();
  json::Value core;
  DHEPZ_CHECK(json::ParseUtf8(std::string_view(ReadFile(root + L"\\assets\\ui\\core.json")),
                              &core)
                  .ok());

  std::vector<ui::config::ScreenSource> sources;
  WIN32_FIND_DATAW entry{};
  const HANDLE find = FindFirstFileW((root + L"\\assets\\ui\\screens\\*.json").c_str(), &entry);
  DHEPZ_CHECK(find != INVALID_HANDLE_VALUE);
  do {
    const std::wstring name(entry.cFileName);
    const std::string text = ReadFile(root + L"\\assets\\ui\\screens\\" + name);
    sources.push_back({name, std::wstring(text.begin(), text.end())});
  } while (FindNextFileW(find, &entry) != 0);
  FindClose(find);

  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  const core::Status status = ui::config::ResolveDocument(core, sources, &diagnostics, &document);
  for (const auto& diagnostic : diagnostics) {
    OutputDebugStringW((diagnostic.message + L"\n").c_str());
  }
  DHEPZ_CHECK(status.ok());
  DHEPZ_CHECK(document->FindRoute(L"home") != nullptr);
}

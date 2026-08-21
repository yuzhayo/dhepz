#include <windows.h>

#include <algorithm>
#include <filesystem>
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

  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::directory_iterator(
           std::filesystem::path(root) / L"assets" / L"ui" / L"screens")) {
    if (entry.is_regular_file() && entry.path().extension() == L".json") {
      paths.push_back(entry.path());
    }
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(
           std::filesystem::path(root) / L"src" / L"modules")) {
    if (entry.is_regular_file() && entry.path().filename() == L"screen.json") {
      paths.push_back(entry.path());
    }
  }
  std::sort(paths.begin(), paths.end());

  std::vector<ui::config::ScreenSource> sources;
  for (const std::filesystem::path& path : paths) {
    const std::string text = ReadFile(path.wstring());
    sources.push_back(
        {path.lexically_relative(root).wstring(),
         std::wstring(text.begin(), text.end())});
  }

  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  const core::Status status = ui::config::ResolveDocument(core, sources, &diagnostics, &document);
  for (const auto& diagnostic : diagnostics) {
    OutputDebugStringW((diagnostic.message + L"\n").c_str());
  }
  DHEPZ_CHECK(status.ok());
  DHEPZ_CHECK(document->FindRoute(L"terminal") != nullptr);
}

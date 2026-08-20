#include "ui/config/ui_schema.h"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>
#include <cstring>

#include "core/json.h"
#include "framework/test_case.h"

namespace {

// The test binary lives at <root>/build/x64/<config>/; the catalog under
// test is the repo's real assets/ui/core.json.
std::string ReadRepoFile(const std::string& relative) {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  std::wstring path(buffer);
  for (int i = 0; i < 4; ++i) {  // file, config dir, x64, build
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    path.resize(slash);
  }
  path += L"\\";
  for (char c : relative) path.push_back(c == '/' ? L'\\' : static_cast<wchar_t>(c));
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream text;
  text << stream.rdbuf();
  return text.str();
}

json::Value LoadCore() {
  json::Value core;
  const core::Status status = json::ParseUtf8(ReadRepoFile("assets/ui/core.json"), &core);
  DHEPZ_CHECK(status.ok());
  return core;
}

json::Value ParseScreenText(const std::string& utf8) {
  json::Value screen;
  const core::Status status = json::ParseUtf8(utf8, &screen);
  DHEPZ_CHECK(status.ok());
  return screen;
}

}  // namespace

DHEPZ_TEST(UiSchema, RepoCoreJsonValidatesClean) {
  const json::Value core = LoadCore();
  std::vector<ui::config::Diagnostic> diagnostics;
  const core::Status status = ui::config::ValidateCore(core, &diagnostics);
  for (const auto& diagnostic : diagnostics) {
    OutputDebugStringW((diagnostic.message + L"\n").c_str());
  }
  DHEPZ_CHECK(status.ok());
  DHEPZ_CHECK(diagnostics.empty());
}

DHEPZ_TEST(UiSchema, CatalogCarriesTheFourteenComponentTypes) {
  const json::Value core = LoadCore();
  const json::Value* components = core.ObjectField(L"components");
  DHEPZ_CHECK(components != nullptr);
  const char* types[14] = {"window", "screen", "container", "text",   "button",  "input",
                           "combo",  "checkbox", "toggle",  "card",   "list",    "scrollbar",
                           "dialog", "tabs"};
  for (const char* type : types) {
    std::wstring wide(type, type + strlen(type));
    DHEPZ_CHECK(components->Find(wide) != nullptr);
  }
}

DHEPZ_TEST(UiSchema, ScreenUsingTenTypesValidatesClean) {
  const json::Value core = LoadCore();
  const json::Value screen = ParseScreenText(R"({
  "components": [
    { "type": "screen", "route_id": "home", "children": [
      { "type": "container", "direction": "column", "gap": 8, "children": [
        { "type": "text", "text": "dhepz", "variant": "title" },
        { "type": "button", "label": "Open", "variant": "primary", "tab_stop": true },
        { "type": "input", "placeholder": "search", "maximum_length": 120 },
        { "type": "combo", "items_binding": "items", "allow_empty": false },
        { "type": "checkbox", "label": "enabled", "tri_state": true },
        { "type": "toggle", "label": "dark", "checked_binding": "dark" },
        { "type": "card", "interactive": true, "children": [
          { "type": "list", "items_binding": "rows", "row_height": 28 }
        ] },
        { "type": "scrollbar", "orientation": "vertical", "thickness": 10 }
      ] }
    ] }
  ]
})");
  std::vector<ui::config::Diagnostic> diagnostics;
  const core::Status status = ui::config::ValidateScreen(core, screen, &diagnostics);
  for (const auto& diagnostic : diagnostics) {
    OutputDebugStringW((diagnostic.message + L"\n").c_str());
  }
  DHEPZ_CHECK(status.ok());
  DHEPZ_CHECK(diagnostics.empty());
}

DHEPZ_TEST(UiSchema, UnknownPropertyFailsWithFileAndLine) {
  const json::Value core = LoadCore();
  // The bogus property sits on line 5 of this document.
  const json::Value screen = ParseScreenText(
      "{\n"
      "  \"components\": [\n"
      "    {\n"
      "      \"type\": \"button\",\n"
      "      \"labell\": \"x\",\n"
      "      \"label\": \"x\"\n"
      "    }\n"
      "  ]\n"
      "}\n");
  std::vector<ui::config::Diagnostic> diagnostics;
  DHEPZ_CHECK(!ui::config::ValidateScreen(core, screen, &diagnostics).ok());
  DHEPZ_CHECK_EQ(diagnostics.size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(diagnostics[0].line), 5ull);
  DHEPZ_CHECK(diagnostics[0].message.find(L"labell") != std::wstring::npos);
}

DHEPZ_TEST(UiSchema, UnknownTypeAndWrongKindAndMissingRequiredAreDiagnostics) {
  const json::Value core = LoadCore();
  const json::Value screen = ParseScreenText(
      "{\n"
      "  \"components\": [\n"
      "    { \"type\": \"widget\" },\n"
      "    { \"type\": \"button\", \"label\": \"ok\", \"tab_stop\": \"yes\" },\n"
      "    { \"type\": \"text\", \"variant\": \"body\" }\n"
      "  ]\n"
      "}\n");
  std::vector<ui::config::Diagnostic> diagnostics;
  DHEPZ_CHECK(!ui::config::ValidateScreen(core, screen, &diagnostics).ok());
  DHEPZ_CHECK_EQ(diagnostics.size(), static_cast<std::size_t>(3));
}

DHEPZ_TEST(UiSchema, ChildrenRefusedOnLeafComponents) {
  const json::Value core = LoadCore();
  const json::Value screen = ParseScreenText(
      "{\n"
      "  \"components\": [\n"
      "    { \"type\": \"button\", \"label\": \"x\", \"children\": [] }\n"
      "  ]\n"
      "}\n");
  std::vector<ui::config::Diagnostic> diagnostics;
  DHEPZ_CHECK(!ui::config::ValidateScreen(core, screen, &diagnostics).ok());
  DHEPZ_CHECK_EQ(diagnostics.size(), static_cast<std::size_t>(1));
}

DHEPZ_TEST(UiSchema, CoreCatalogRejectsBadKindAndEnumWithoutValues) {
  json::Value core;
  DHEPZ_CHECK(json::ParseUtf8(
                  R"({ "schema": "dhepz.ui.core", "version": 1,
     "tokens": { "dark": { "text": "#FFFFFF" }, "light": { "text": "#000000" } },
     "components": {
       "thing": { "properties": {
         "a": { "kind": "colour" },
         "b": { "kind": "enum" } } } } })",
                  &core)
                  .ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  DHEPZ_CHECK(!ui::config::ValidateCore(core, &diagnostics).ok());
  DHEPZ_CHECK(diagnostics.size() >= 2);
}

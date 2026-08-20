#include "ui/config/resolved_ui_document.h"

#include <windows.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/json.h"
#include "framework/test_case.h"

namespace {

json::Value LoadCore() {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  std::wstring path(buffer);
  for (int i = 0; i < 4; ++i) {
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    path.resize(slash);
  }
  path += L"\\assets\\ui\\core.json";
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream text;
  text << stream.rdbuf();
  const std::string utf8 = text.str();
  json::Value core;
  DHEPZ_CHECK(json::ParseUtf8(std::string_view(utf8), &core).ok());
  return core;
}

std::wstring W(const char* utf8) {
  std::wstring wide;
  for (const char* p = utf8; *p != '\0'; ++p) {
    wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
  }
  return wide;
}

const char* kEmbedded = R"({
  "components": [
    { "type": "window", "title": "dhepz", "initial_route": "home" },
    { "type": "screen", "route_id": "home", "tab_label": "Home", "children": [
      { "type": "button", "label": "Go" }
    ] },
    { "type": "screen", "route_id": "about", "children": [
      { "type": "text", "text": "dhepz" }
    ] }
  ]
})";

int WalkCount(const ui::config::ComponentNode& node) {
  int count = 1;
  for (const ui::config::ComponentNode& child : node.children()) {
    count += WalkCount(child);
  }
  return count;
}

}  // namespace

DHEPZ_TEST(Resolver, ResolvesRoutesDefaultsAndTokens) {
  const json::Value core = LoadCore();
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(core, {{L"embedded", W(kEmbedded)}}, &diagnostics,
                                          &document)
                  .ok());
  DHEPZ_CHECK(document != nullptr);

  DHEPZ_CHECK_EQ(document->routes().size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(document->initial_route(), std::wstring(L"home"));
  const ui::config::Route* home = document->FindRoute(L"home");
  DHEPZ_CHECK(home != nullptr);
  DHEPZ_CHECK_EQ(home->tab_label, std::wstring(L"Home"));
  DHEPZ_CHECK(home->show_in_tabs);

  // Typed accessors: explicit value plus catalog defaults.
  const ui::config::ComponentNode& button = home->root.children()[0];
  DHEPZ_CHECK_EQ(button.type(), std::wstring(L"button"));
  DHEPZ_CHECK_EQ(button.GetString(L"label"), std::wstring(L"Go"));
  DHEPZ_CHECK(button.GetBool(L"tab_stop"));             // default from the catalog
  DHEPZ_CHECK_EQ(button.GetString(L"variant"), std::wstring(L"default"));
  DHEPZ_CHECK_EQ(button.GetInt(L"press_selects"), 0ll);

  ui::config::Rgba accent{};
  DHEPZ_CHECK(document->Token(L"dark", L"accent", &accent));
  DHEPZ_CHECK_EQ(static_cast<int>(accent.r), 0x60);
  DHEPZ_CHECK_EQ(static_cast<int>(accent.g), 0xA5);
  DHEPZ_CHECK_EQ(static_cast<int>(accent.b), 0xFA);
}

DHEPZ_TEST(Resolver, OverrideReplacesEmbeddedRouteAndLosesToNothing) {
  const json::Value core = LoadCore();
  const char* override_text = R"({
    "components": [
      { "type": "screen", "route_id": "home", "tab_label": "Rumah", "children": [
        { "type": "text", "text": "halo" }
      ] }
    ]
  })";
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(core,
                                          {{L"embedded", W(kEmbedded)},
                                           {L"override", W(override_text)}},
                                          &diagnostics, &document)
                  .ok());
  // Two routes: home replaced in place, about kept from embedded.
  DHEPZ_CHECK_EQ(document->routes().size(), static_cast<std::size_t>(2));
  const ui::config::Route* home = document->FindRoute(L"home");
  DHEPZ_CHECK(home != nullptr);
  DHEPZ_CHECK_EQ(home->tab_label, std::wstring(L"Rumah"));
  DHEPZ_CHECK_EQ(home->root.children()[0].type(), std::wstring(L"text"));
}

DHEPZ_TEST(Resolver, MalformedJsonReportsSourceAndLine) {
  const json::Value core = LoadCore();
  const char* broken = "{\n  \"components\": [\n}\n";
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(!ui::config::ResolveDocument(core, {{L"override", W(broken)}}, &diagnostics,
                                           &document)
                   .ok());
  DHEPZ_CHECK_EQ(diagnostics.size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(diagnostics[0].line), 3ull);
  DHEPZ_CHECK(diagnostics[0].message.find(L"override") != std::wstring::npos);
}

DHEPZ_TEST(Resolver, DuplicateRouteInOneSourceIsADiagnostic) {
  const json::Value core = LoadCore();
  const char* duplicate = R"({
    "components": [
      { "type": "screen", "route_id": "home" },
      { "type": "screen", "route_id": "home" }
    ]
  })";
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(!ui::config::ResolveDocument(core, {{L"embedded", W(duplicate)}}, &diagnostics,
                                           &document)
                   .ok());
  DHEPZ_CHECK_EQ(diagnostics.size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK(diagnostics[0].message.find(L"duplicate route") != std::wstring::npos);
}

DHEPZ_TEST(Resolver, MissingInitialRouteFails) {
  const json::Value core = LoadCore();
  const char* bad_route = R"({
    "components": [
      { "type": "window", "title": "x", "initial_route": "nowhere" },
      { "type": "screen", "route_id": "home" }
    ]
  })";
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(!ui::config::ResolveDocument(core, {{L"embedded", W(bad_route)}}, &diagnostics,
                                           &document)
                   .ok());
}

DHEPZ_TEST(Resolver, ResolvedDocumentFeedsATreeWalkingConsumer) {
  // Integration stand-in for the layout pipeline (#56 re-verifies the real
  // handoff): the document must be consumable as an immutable tree.
  const json::Value core = LoadCore();
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(core, {{L"embedded", W(kEmbedded)}}, &diagnostics,
                                          &document)
                  .ok());
  int total = 0;
  for (const ui::config::Route& route : document->routes()) {
    total += WalkCount(route.root);
  }
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(total), 4ull);  // 2 screens + 2 children
}

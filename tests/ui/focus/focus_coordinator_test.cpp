#include "ui/focus/focus_coordinator.h"

#include <string>
#include <vector>

#include "core/json.h"
#include "framework/test_case.h"
#include "ui/config/resolved_ui_document.h"

namespace {

const char* kCore = R"({
  "schema": "dhepz.ui.core",
  "version": 1,
  "tokens": { "dark": { "text": "#FFFFFF" }, "light": { "text": "#000000" } },
  "common": { "properties": {
    "id": { "kind": "string" },
    "visible": { "kind": "bool", "default": true },
    "enabled": { "kind": "bool", "default": true }
  } },
  "allows_children": ["screen", "container"],
  "components": {
    "screen": { "properties": {
      "route_id": { "kind": "string", "required": true }
    } },
    "container": { "properties": {
      "tab_stop": { "kind": "bool", "default": false }
    } },
    "button": { "properties": {
      "label": { "kind": "text", "required": true },
      "tab_stop": { "kind": "bool", "default": true }
    } }
  }
})";

const char* kScreens = R"({
  "components": [
    { "type": "screen", "route_id": "home", "children": [
      { "type": "button", "id": "a", "label": "A" },
      { "type": "button", "id": "b", "label": "B", "tab_stop": false },
      { "type": "button", "id": "c", "label": "C", "enabled": false },
      { "type": "container", "children": [
        { "type": "button", "id": "d", "label": "D" },
        { "type": "button", "label": "E" }
      ] }
    ] },
    { "type": "screen", "route_id": "other", "children": [
      { "type": "button", "id": "x", "label": "X" }
    ] }
  ]
})";

std::wstring W(const char* utf8) {
  std::wstring wide;
  for (const char* p = utf8; *p != '\0'; ++p) {
    wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
  }
  return wide;
}

std::unique_ptr<ui::config::ResolvedUiDocument> Resolve() {
  json::Value core;
  DHEPZ_CHECK(json::Parse(W(kCore), &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(core, {{L"embedded", W(kScreens)}}, &diagnostics,
                                          &document)
                  .ok());
  return document;
}

}  // namespace

DHEPZ_TEST(FocusCoordinator, TraversalSkipsUnfocusableAndKeepsDeclarationOrder) {
  ui::focus::FocusCoordinator coordinator;
  coordinator.SetDocument(Resolve().get());
  const std::vector<std::wstring> order = coordinator.Focusables(L"home");
  DHEPZ_CHECK_EQ(order.size(), static_cast<std::size_t>(3));  // a, d, synthetic E
  DHEPZ_CHECK_EQ(order[0], std::wstring(L"a"));
  DHEPZ_CHECK_EQ(order[1], std::wstring(L"d"));
  DHEPZ_CHECK(order[2].find(L"@") != std::wstring::npos);
}

DHEPZ_TEST(FocusCoordinator, AdvanceWrapsBothDirections) {
  const std::unique_ptr<ui::config::ResolvedUiDocument> document = Resolve();
  ui::focus::FocusCoordinator coordinator;
  coordinator.SetDocument(document.get());
  coordinator.EnterRoute(L"home");
  DHEPZ_CHECK_EQ(coordinator.Current(L"home"), std::wstring(L"a"));

  DHEPZ_CHECK_EQ(coordinator.Advance(L"home", false), std::wstring(L"d"));
  DHEPZ_CHECK(coordinator.Advance(L"home", false).find(L"@") != std::wstring::npos);
  DHEPZ_CHECK_EQ(coordinator.Advance(L"home", false), std::wstring(L"a"));  // wrap
  DHEPZ_CHECK(coordinator.Advance(L"home", true).find(L"@") != std::wstring::npos);  // back-wrap
}

DHEPZ_TEST(FocusCoordinator, RouteSwitchRestoresSavedFocusOrFallsBack) {
  const std::unique_ptr<ui::config::ResolvedUiDocument> document = Resolve();
  ui::focus::FocusCoordinator coordinator;
  coordinator.SetDocument(document.get());

  coordinator.EnterRoute(L"home");
  DHEPZ_CHECK(coordinator.SetFocus(L"home", L"d"));
  coordinator.EnterRoute(L"other");
  DHEPZ_CHECK_EQ(coordinator.Current(L"other"), std::wstring(L"x"));
  coordinator.EnterRoute(L"home");
  DHEPZ_CHECK_EQ(coordinator.Current(L"home"), std::wstring(L"d"));  // restored

  // A document where the saved id vanished falls back to the first.
  coordinator.SetDocument(document.get());
  coordinator.EnterRoute(L"home");
  DHEPZ_CHECK_EQ(coordinator.Current(L"home"), std::wstring(L"a"));
}

DHEPZ_TEST(FocusCoordinator, SetFocusRefusesUnfocusableIds) {
  const std::unique_ptr<ui::config::ResolvedUiDocument> document = Resolve();
  ui::focus::FocusCoordinator coordinator;
  coordinator.SetDocument(document.get());
  DHEPZ_CHECK_FALSE(coordinator.SetFocus(L"home", L"b"));  // tab_stop false
  DHEPZ_CHECK_FALSE(coordinator.SetFocus(L"home", L"c"));  // disabled
  DHEPZ_CHECK_FALSE(coordinator.SetFocus(L"home", L"nope"));
  DHEPZ_CHECK(coordinator.SetFocus(L"home", L"a"));
}

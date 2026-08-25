#include <windows.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "core/json.h"
#include "framework/test_case.h"
#include "parent/ui/config/resolved_ui_document.h"
#include "parent/ui/config/ui_schema.h"
#include "parent/ui/contracts/ui_action_registry.h"
#include "parent/ui/contracts/ui_state.h"
#include "parent/ui/runtime/parent_ui.h"
#include "platform/files.h"
#include "ui/app_window/app_window.h"
#include "ui/components/component_registry.h"

namespace {

std::filesystem::path RepositoryRoot() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

json::Value LoadJson(const std::filesystem::path& path) {
  std::wstring text;
  const core::Status read = files::ReadText(path.wstring(), &text);
  if (!read.ok()) {
    testing::Fail("could not read P3 JSON asset", __FILE__, __LINE__);
  }
  json::Value value;
  const core::Status parsed = json::Parse(text, &value);
  if (!parsed.ok()) {
    testing::Fail("could not parse P3 JSON asset", __FILE__, __LINE__);
  }
  return value;
}

std::unique_ptr<ui::config::ResolvedUiDocument> Resolve(
    const json::Value& core_catalog, std::wstring screen) {
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  const core::Status status = ui::config::ResolveDocument(
      core_catalog, {{L"test-screen", std::move(screen)}}, &diagnostics, &document);
  if (!status.ok()) {
    testing::Fail("could not resolve P3 UI document", __FILE__, __LINE__);
  }
  return document;
}

}  // namespace

DHEPZ_TEST(P3Ui, CatalogResolverAndRegistryStayAligned) {
  json::Value core_catalog = LoadJson(RepositoryRoot() / L"assets" / L"ui" / L"core.json");
  const json::Value* catalog = core_catalog.ObjectField(L"components");
  DHEPZ_CHECK(catalog != nullptr);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(catalog->members().size()), 14ULL);

  constexpr std::array<std::wstring_view, 14> types{
      L"window", L"screen",    L"container", L"text",   L"button",
      L"input",  L"combo",     L"checkbox",  L"toggle", L"card",
      L"list",   L"scrollbar", L"dialog",    L"tabs"};
  ui::components::ComponentRegistry registry;
  for (const std::wstring_view type : types) {
    DHEPZ_CHECK(catalog->Find(type) != nullptr);
    DHEPZ_CHECK(registry.Find(type) != nullptr);
  }

  std::vector<ui::config::Diagnostic> diagnostics;
  DHEPZ_CHECK(ui::config::ValidateCore(core_catalog, &diagnostics).ok());
  DHEPZ_CHECK(diagnostics.empty());

  const json::Value settings =
      LoadJson(RepositoryRoot() / L"assets" / L"ui" / L"screens" / L"settings.json");
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(
                  core_catalog, {{L"settings.json", json::Serialize(settings, false)}},
                  &diagnostics, &document)
                  .ok());
  DHEPZ_CHECK(document != nullptr);
  DHEPZ_CHECK_EQ(document->initial_route(), std::wstring(L"settings"));

  json::Value invalid = core_catalog;
  invalid.Find(L"components")->Remove(L"tabs");
  diagnostics.clear();
  DHEPZ_CHECK_FALSE(ui::config::ValidateCore(invalid, &diagnostics).ok());
  DHEPZ_CHECK_FALSE(diagnostics.empty());
}

DHEPZ_TEST(P3Ui, BindingActionAndPatchFormOneContract) {
  ui::config::ComponentNode checkbox(L"checkbox", L"venv-toggle");
  checkbox.SetProperty(L"checked_binding", json::Value::String(L"viewState.venv"));
  checkbox.SetProperty(L"action", json::Value::String(L"settings.venv.changed"));

  ui::application::UiState state;
  state.Set(L"viewState.venv", false);
  ui::components::ComponentRegistry components;
  const ui::components::ComponentDescriptor* descriptor = components.Find(L"checkbox");
  DHEPZ_CHECK(descriptor != nullptr);
  DHEPZ_CHECK(descriptor->activate != nullptr);

  const ui::components::ComponentResult result = descriptor->activate(checkbox, state);
  DHEPZ_CHECK(result.handled);
  DHEPZ_CHECK_EQ(result.event.action, std::wstring(L"settings.venv.changed"));
  DHEPZ_CHECK(state.Apply(result.patch));
  DHEPZ_CHECK(state.Bool(L"viewState.venv"));

  ui::application::UiActionRegistry actions;
  DHEPZ_CHECK(actions.Register(
      L"settings.venv.changed",
      [](const ui::application::UiEvent& event, const ui::application::UiState&) {
        const bool* enabled = std::get_if<bool>(&event.payload);
        ui::application::UiPatch patch;
        patch.changes.push_back(
            {L"viewState.status", std::wstring(enabled != nullptr && *enabled ? L"on" : L"off")});
        return patch;
      }));
  DHEPZ_CHECK(state.Apply(actions.Dispatch(result.event, state)));
  DHEPZ_CHECK_EQ(state.Text(L"viewState.status"), std::wstring(L"on"));
}

DHEPZ_TEST(P3Ui, ResolvedRoutesCarryTabMetadataInDocumentOrder) {
  const json::Value core_catalog =
      LoadJson(RepositoryRoot() / L"assets" / L"ui" / L"core.json");
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(
                  core_catalog,
                  {{L"routes.json",
                    LR"({"components":[{"type":"screen","route_id":"terminal","tab_label":"Terminal","show_in_tabs":true},{"type":"screen","route_id":"settings","show_in_tabs":false}]})"}},
                  &diagnostics, &document)
                  .ok());
  DHEPZ_CHECK(document != nullptr);
  DHEPZ_CHECK_EQ(document->routes().size(), 2ULL);
  DHEPZ_CHECK_EQ(document->routes()[0].id, std::wstring(L"terminal"));
  DHEPZ_CHECK_EQ(document->routes()[0].tab_label, std::wstring(L"Terminal"));
  DHEPZ_CHECK(document->routes()[0].show_in_tabs);
  DHEPZ_CHECK_FALSE(document->routes()[1].show_in_tabs);
}

DHEPZ_TEST(P3Ui, AppWindowRoutesContentInputThroughParentUi) {
  const json::Value core_catalog =
      LoadJson(RepositoryRoot() / L"assets" / L"ui" / L"core.json");
  std::unique_ptr<ui::config::ResolvedUiDocument> document = Resolve(
      core_catalog,
      LR"({"components":[{"type":"screen","route_id":"test","children":[{"type":"button","id":"apply","label":"Apply","action":"settings.apply"}]}]})");

  bool invoked = false;
  ui::application::UiActionRegistry actions;
  DHEPZ_CHECK(actions.Register(
      L"settings.apply",
      [&invoked](const ui::application::UiEvent&, const ui::application::UiState&) {
        invoked = true;
        return ui::application::UiPatch{};
      }));

  shell::AppWindow window;
  DHEPZ_CHECK(window.Create(GetModuleHandleW(nullptr), 320.0f, 240.0f));
  ui::containers::ParentUi parent;
  DHEPZ_CHECK(parent.Attach(&window, document.get(), &actions).ok());
  window.Show();

  const float scale = window.backend()->dpi() / 96.0f;
  const int x = static_cast<int>(std::lround(34.0f * scale));
  const int y = static_cast<int>(std::lround(74.0f * scale));
  const HWND hwnd = static_cast<HWND>(window.hwnd());
  SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
  SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
  DHEPZ_CHECK(invoked);

  parent.Detach();
  window.Close();
}

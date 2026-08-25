#include <windows.h>

#include <filesystem>
#include <memory>

#include "core/json.h"
#include "framework/test_case.h"
#include "parent/ui/config/resolved_ui_document.h"
#include "parent/ui/contracts/ui_state.h"
#include "parent/ui/runtime/route_tabs.h"
#include "platform/files.h"
#include "render/gdi_backend.h"
#include "ui/components/component_registry.h"

namespace {

std::filesystem::path RepositoryRoot() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

std::unique_ptr<ui::config::ResolvedUiDocument> Document() {
  std::wstring core_text;
  DHEPZ_CHECK(files::ReadText((RepositoryRoot() / L"assets" / L"ui" / L"core.json").wstring(),
                              &core_text)
                  .ok());
  json::Value core;
  DHEPZ_CHECK(json::Parse(core_text, &core).ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(
                  core,
                  {{L"tabs.json",
                    LR"({"components":[{"type":"screen","route_id":"one","tab_label":"One"},{"type":"screen","route_id":"hidden","show_in_tabs":false},{"type":"screen","route_id":"two","tab_label":"Two"}]})"}},
                  &diagnostics, &document)
                  .ok());
  return document;
}

std::filesystem::path TempState() {
  return std::filesystem::temp_directory_path() /
         (L"dhepz-tabs-" + std::to_wstring(GetCurrentProcessId()) + L".json");
}

}  // namespace

DHEPZ_TEST(RouteTabs, ResolvesVisibleRoutesSelectsAndHonorsLock) {
  const std::filesystem::path state = TempState();
  std::error_code ignored;
  std::filesystem::remove(state, ignored);
  ui::tabs::RouteTabs tabs(state.wstring());
  DHEPZ_CHECK(tabs.Load().ok());
  tabs.Resolve(*Document());
  DHEPZ_CHECK_EQ(tabs.order().size(), 2ULL);
  DHEPZ_CHECK_EQ(tabs.order()[0], std::wstring(L"one"));
  DHEPZ_CHECK_EQ(tabs.labels()[1], std::wstring(L"Two"));
  DHEPZ_CHECK_EQ(tabs.Select(L"two").route, std::wstring(L"two"));
  DHEPZ_CHECK(tabs.Select(L"hidden").empty());
  DHEPZ_CHECK(tabs.SetLocked(true));
  DHEPZ_CHECK_FALSE(tabs.Reorder(0, 1));
  std::filesystem::remove(state, ignored);
}

DHEPZ_TEST(RouteTabs, PersistsOrderLockAndRowMode) {
  const std::filesystem::path state = TempState();
  std::error_code ignored;
  std::filesystem::remove(state, ignored);
  {
    ui::tabs::RouteTabs tabs(state.wstring());
    tabs.Resolve(*Document());
    DHEPZ_CHECK(tabs.Reorder(0, 1));
    DHEPZ_CHECK(tabs.SetLocked(true));
    DHEPZ_CHECK(tabs.SetMultiRow(false));
    DHEPZ_CHECK(tabs.Save().ok());
  }
  {
    ui::tabs::RouteTabs tabs(state.wstring());
    DHEPZ_CHECK(tabs.Load().ok());
    tabs.Resolve(*Document());
    DHEPZ_CHECK_EQ(tabs.order()[0], std::wstring(L"two"));
    DHEPZ_CHECK(tabs.locked());
    DHEPZ_CHECK_FALSE(tabs.multi_row());
  }
  std::filesystem::remove(state, ignored);
}

DHEPZ_TEST(RouteTabs, ComponentWrapsAndEmitsParentActions) {
  ui::config::ComponentNode node(L"tabs", L"route-tabs");
  node.SetProperty(L"routes_binding", json::Value::String(L"parent.tabs.routes"));
  node.SetProperty(L"labels_binding", json::Value::String(L"parent.tabs.labels"));
  node.SetProperty(L"selected_binding", json::Value::String(L"parent.tabs.selected"));
  node.SetProperty(L"locked_binding", json::Value::String(L"parent.tabs.locked"));
  node.SetProperty(L"multi_row_binding", json::Value::String(L"parent.tabs.multi_row"));
  node.SetProperty(L"scroll_binding", json::Value::String(L"parent.tabs.scroll_offset"));
  node.SetProperty(L"select_action", json::Value::String(L"parent.tabs.select"));
  node.SetProperty(L"lock_action", json::Value::String(L"parent.tabs.lock"));

  const std::vector<std::wstring> routes{L"one", L"two", L"three"};
  ui::application::UiState state;
  state.Set(L"parent.tabs.routes", routes);
  state.Set(L"parent.tabs.labels", routes);
  state.Set(L"parent.tabs.selected", std::wstring(L"one"));
  state.Set(L"parent.tabs.locked", false);
  state.Set(L"parent.tabs.multi_row", true);

  render::GdiBackend backend;
  ui::components::ComponentRegistry registry;
  const ui::components::ComponentDescriptor* tabs = registry.Find(L"tabs");
  DHEPZ_CHECK(tabs != nullptr && tabs->measure != nullptr);
  const render::Size wrapped = tabs->measure(node, backend, state, 260.0f);
  state.Set(L"parent.tabs.multi_row", false);
  const render::Size single = tabs->measure(node, backend, state, 260.0f);
  DHEPZ_CHECK(wrapped.height > single.height);

  const render::Rect overflow_bounds{0.0f, 0.0f, 260.0f, single.height};
  const ui::components::ComponentResult scroll =
      tabs->pointer_down(node, state, {150.0f, single.height - 2.0f}, overflow_bounds);
  DHEPZ_CHECK(state.Apply(scroll.patch));
  DHEPZ_CHECK(state.Integer(L"parent.tabs.scroll_offset") > 0);
  DHEPZ_CHECK(state.Apply(tabs->pointer_up(
      node, state, {150.0f, single.height - 2.0f}, overflow_bounds).patch));
  state.Set(L"parent.tabs.scroll_offset", 0LL);

  const render::Rect bounds{0.0f, 0.0f, 260.0f, single.height};
  DHEPZ_CHECK(tabs->pointer_down != nullptr && tabs->pointer_up != nullptr);
  const ui::components::ComponentResult down =
      tabs->pointer_down(node, state, {24.0f, 10.0f}, bounds);
  DHEPZ_CHECK(state.Apply(down.patch));
  const ui::components::ComponentResult selected =
      tabs->pointer_up(node, state, {24.0f, 10.0f}, bounds);
  DHEPZ_CHECK_EQ(selected.event.action, std::wstring(L"parent.tabs.select"));
  DHEPZ_CHECK_EQ(std::get<std::wstring>(selected.event.payload), std::wstring(L"one"));

  const ui::components::ComponentResult lock_down =
      tabs->pointer_down(node, state, {240.0f, 10.0f}, bounds);
  DHEPZ_CHECK(state.Apply(lock_down.patch));
  const ui::components::ComponentResult locked =
      tabs->pointer_up(node, state, {240.0f, 10.0f}, bounds);
  DHEPZ_CHECK_EQ(locked.event.action, std::wstring(L"parent.tabs.lock"));
  DHEPZ_CHECK(std::get<bool>(locked.event.payload));
}

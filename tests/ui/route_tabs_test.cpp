#include <windows.h>

#include <filesystem>
#include <memory>

#include "core/json.h"
#include "framework/test_case.h"
#include "parent/ui/config/resolved_ui_document.h"
#include "parent/ui/runtime/route_tabs.h"
#include "platform/files.h"

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

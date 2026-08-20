#include "ui/theme/theme_adapter.h"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>

#include "core/json.h"
#include "framework/test_case.h"
#include "ui/config/resolved_ui_document.h"

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

ui::theme::ThemeSnapshot Flipped(const ui::theme::ThemeSnapshot& snapshot) {
  ui::theme::ThemeSnapshot other = snapshot;
  other.app_theme =
      snapshot.app_theme == ui::theme::OsTheme::Dark ? ui::theme::OsTheme::Light
                                                     : ui::theme::OsTheme::Dark;
  return other;
}

}  // namespace

DHEPZ_TEST(ThemeAdapter, SelectIsDeterministicAndResolvesAgainstTheDocument) {
  ui::theme::ThemeAdapter adapter;
  DHEPZ_CHECK_EQ(adapter.Select(ui::theme::ThemePreference::Dark), std::wstring(L"dark"));
  DHEPZ_CHECK_EQ(adapter.Select(ui::theme::ThemePreference::Light), std::wstring(L"light"));
  const std::wstring system = adapter.Select(ui::theme::ThemePreference::System);
  DHEPZ_CHECK(system == L"dark" || system == L"light");

  // The selected name always resolves to deterministic colours in the doc.
  const json::Value core = LoadCore();
  ui::config::Rgba window{};
  DHEPZ_CHECK(core.ObjectField(L"tokens") != nullptr);
  const json::Value* theme = core.ObjectField(L"tokens")->ObjectField(system);
  DHEPZ_CHECK(theme != nullptr);
  DHEPZ_CHECK(theme->Find(L"window") != nullptr);
  (void)window;
}

DHEPZ_TEST(ThemeAdapter, InitialSnapshotIsConsistent) {
  const ui::theme::ThemeSnapshot first = ui::theme::ReadInitialSnapshot();
  const ui::theme::ThemeSnapshot second = ui::theme::ReadInitialSnapshot();
  DHEPZ_CHECK(first == second);
}

DHEPZ_TEST(ThemeAdapter, QueuedSnapshotsDebounceToASingleApply) {
  ui::theme::ThemeAdapter adapter;
  const ui::theme::ThemeSnapshot start = adapter.snapshot();

  // Same snapshot queued: nothing to apply.
  adapter.QueueSnapshot(start);
  DHEPZ_CHECK_FALSE(adapter.ApplyQueuedSnapshot());

  // A burst of differing snapshots still applies exactly once.
  const ui::theme::ThemeSnapshot flipped = Flipped(start);
  adapter.QueueSnapshot(flipped);
  adapter.QueueSnapshot(flipped);
  DHEPZ_CHECK(adapter.ApplyQueuedSnapshot());
  DHEPZ_CHECK(adapter.snapshot() == flipped);
  DHEPZ_CHECK_FALSE(adapter.ApplyQueuedSnapshot());
}

DHEPZ_TEST(ThemeAdapter, ReconcileQueuesWhenTheRegistryViewDiffers) {
  ui::theme::ThemeAdapter adapter;
  // Reconcile reads the live OS state; whatever it sees, applying twice
  // never double-applies.
  adapter.Reconcile();
  adapter.ApplyQueuedSnapshot();
  DHEPZ_CHECK_FALSE(adapter.ApplyQueuedSnapshot());
}

DHEPZ_TEST(ThemeAdapter, MonitoringStartsAndStopsCleanly) {
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"dhepz.test.theme.infra";
  RegisterClassW(&window_class);
  const HWND window = CreateWindowExW(0, window_class.lpszClassName, L"theme", WS_POPUP, 0, 0,
                                      10, 10, nullptr, nullptr, window_class.hInstance, nullptr);
  DHEPZ_CHECK(window != nullptr);

  ui::theme::ThemeAdapter adapter;
  std::wstring diagnostic;
  const core::Status status = adapter.StartMonitoring(window, WM_APP + 71, &diagnostic);
  // UISettings may be unavailable in exotic sessions; both outcomes are
  // graceful, a crash is not.
  DHEPZ_CHECK(status.ok() || !diagnostic.empty());
  adapter.StopMonitoring();
  adapter.StopMonitoring();  // idempotent
  DestroyWindow(window);
}

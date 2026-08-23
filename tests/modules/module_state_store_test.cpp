#include <filesystem>
#include <windows.h>

#include "framework/test_case.h"
#include "parent/logic/module_state_store.h"
#include "parent/ui/contracts/ui_state.h"

namespace {

std::filesystem::path TempStateFile() {
  return std::filesystem::temp_directory_path() /
         (L"dhepz-module-state-" + std::to_wstring(GetCurrentProcessId()) + L".json");
}

}  // namespace

DHEPZ_TEST(ModuleStateStore, RoundTripsTerminalPathAndHistory) {
  const std::filesystem::path path = TempStateFile();
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  modules::ModuleStateStore writer(path.wstring());
  ui::application::UiPatch patch;
  patch.changes.push_back({L"terminal.path", std::wstring(L"C:\\saved")});
  patch.changes.push_back(
      {L"terminal.recent_paths", std::vector<std::wstring>{L"C:\\saved", L"D:\\old"}});
  DHEPZ_CHECK(writer.Save(patch).ok());

  modules::ModuleStateStore reader(path.wstring());
  DHEPZ_CHECK(reader.Load().ok());
  ui::application::UiState state;
  DHEPZ_CHECK(state.Apply(reader.Restore(L"terminal.")));
  DHEPZ_CHECK_EQ(state.Text(L"terminal.path"), std::wstring(L"C:\\saved"));
  const std::vector<std::wstring>* history = state.Strings(L"terminal.recent_paths");
  DHEPZ_CHECK(history != nullptr);
  DHEPZ_CHECK_EQ(history->size(), static_cast<std::size_t>(2));

  std::filesystem::remove(path, ignored);
}

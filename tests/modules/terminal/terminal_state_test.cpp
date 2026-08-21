#include "modules/terminal/terminal_state.h"

#include <windows.h>

#include <cstdio>

#include <map>
#include <string>

#include "framework/test_case.h"

namespace {

class FakeHost final : public modules::ModuleHost {
 public:
  modules::ModuleSurface Surface() override { return {}; }
  core::Status SettingsRead(std::wstring_view key, std::wstring* out) override {
    const auto it = settings.find(std::wstring(key));
    if (it == settings.end()) return core::Err(core::ErrorCode::NotFound, L"no key");
    *out = it->second;
    return core::Ok();
  }
  core::Status SettingsReadGlobal(std::wstring_view, std::wstring*) override {
    return core::Err(core::ErrorCode::NotFound, L"no global");
  }
  core::Status SettingsWrite(std::wstring_view key, std::wstring_view value) override {
    settings[std::wstring(key)] = std::wstring(value);
    return core::Ok();
  }
  core::Status StorageWrite(std::wstring_view, std::wstring_view) override { return core::Ok(); }
  core::Status StorageRead(std::wstring_view, std::wstring*) override {
    return core::Err(core::ErrorCode::NotFound, L"no blob");
  }
  core::Status StartProcess(const modules::ProcessRequest&, modules::HostOperationCallback,
                            modules::AsyncRequestToken*) override {
    return core::Err(core::ErrorCode::Unsupported, L"not here");
  }
  core::Status StartFolderProbe(const modules::FolderProbeRequest&,
                                modules::HostOperationCallback,
                                modules::AsyncRequestToken*) override {
    return core::Err(core::ErrorCode::Unsupported, L"not here");
  }
  void CancelRequest(modules::AsyncRequestToken) override {}
  core::Status PublishStatePatch(const json::Value&) override { return core::Ok(); }
  core::Status GetSettingsAllFacet(modules::SettingsAllFacet** facet) override {
    *facet = nullptr;
    return core::Err(core::ErrorCode::PermissionDenied, L"not granted");
  }
  core::Status GetConfigWriteFacet(modules::ConfigWriteFacet** facet) override {
    *facet = nullptr;
    return core::Err(core::ErrorCode::PermissionDenied, L"not granted");
  }
  void ReportStatus(const core::Status&) override {}
  void Log(std::wstring_view, std::wstring_view) override {}
  core::Status RequestRoute(std::wstring_view) override { return core::Ok(); }
  std::vector<modules::PeerInfo> Peers() override { return {}; }

  std::map<std::wstring, std::wstring> settings;
};

std::wstring TempDir(const wchar_t* name) {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  std::wstring path = std::wstring(base) + L"dhepz-p4-" + name + L"-" +
                    std::to_wstring(GetTickCount());
  CreateDirectoryW(path.c_str(), nullptr);
  return path;
}

}  // namespace

DHEPZ_TEST(TerminalState, RecentFoldersDedupeCapAndOrder) {
  terminal::RecentFolders recent;
  for (int i = 0; i < 12; ++i) {
    recent.Add(L"C:\\work\\" + std::to_wstring(i));
  }
  DHEPZ_CHECK_EQ(recent.List().size(), static_cast<std::size_t>(10));
  DHEPZ_CHECK_EQ(recent.List()[0], std::wstring(L"C:\\work\\11"));

  recent.Add(L"C:\\work\\5");  // dedupe moves to front
  DHEPZ_CHECK_EQ(recent.List()[0], std::wstring(L"C:\\work\\5"));
  DHEPZ_CHECK_EQ(recent.List().size(), static_cast<std::size_t>(10));

  recent.Remove(L"C:\\work\\5");
  DHEPZ_CHECK_EQ(recent.List()[0], std::wstring(L"C:\\work\\11"));
}

DHEPZ_TEST(TerminalState, RecentFoldersPersistThroughOwnSection) {
  FakeHost host;
  terminal::RecentFolders recent;
  recent.Add(L"C:\\a");
  recent.Add(L"C:\\b");
  recent.Save(host);

  terminal::RecentFolders loaded;
  loaded.Load(host);
  DHEPZ_CHECK_EQ(loaded.List().size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(loaded.List()[0], std::wstring(L"C:\\b"));
}

DHEPZ_TEST(TerminalState, VenvDetectionOnStandardLayouts) {
  const std::wstring folder = TempDir(L"venv");
  DHEPZ_CHECK(!terminal::DetectVenv(folder));
  CreateDirectoryW((folder + L"\\Scripts").c_str(), nullptr);
  HANDLE file = CreateFileW((folder + L"\\Scripts\\activate.bat").c_str(), GENERIC_WRITE, 0,
                            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  DHEPZ_CHECK(file != INVALID_HANDLE_VALUE);
  CloseHandle(file);
  DHEPZ_CHECK(terminal::DetectVenv(folder));
  DHEPZ_CHECK(terminal::VenvActivatePath(folder).find(L"activate.bat") != std::wstring::npos);
}

DHEPZ_TEST(TerminalState, ValidateFolderDiagnostics) {
  const std::wstring folder = TempDir(L"valid");
  DHEPZ_CHECK(terminal::ValidateFolder(folder).ok());

  const core::Status missing = terminal::ValidateFolder(folder + L"\\nope");
  DHEPZ_CHECK(!missing.ok());

  const std::wstring file_path = folder + L"\\file.txt";
  HANDLE file = CreateFileW(file_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  CloseHandle(file);
  const core::Status not_dir = terminal::ValidateFolder(file_path);
  DHEPZ_CHECK(!not_dir.ok());
}

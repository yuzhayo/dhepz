#include "platform/explorer_context_menu.h"

#include <windows.h>

#include <string>

#include "platform/strings.h"

namespace explorer_context_menu {
namespace {

constexpr wchar_t kDirectoryKey[] = L"Software\\Classes\\Directory\\shell\\Dhepz";
constexpr wchar_t kBackgroundKey[] =
    L"Software\\Classes\\Directory\\Background\\shell\\Dhepz";

core::Status RegistryFailure(std::wstring_view operation, LSTATUS error) {
  return DHEPZ_ERR(core::ErrorCode::Internal,
                   std::wstring(operation) + L" failed with registry error " +
                       std::to_wstring(error));
}

core::Status SetText(HKEY key, const wchar_t* name, std::wstring_view value) {
  const LSTATUS result = RegSetValueExW(
      key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.data()),
      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  return result == ERROR_SUCCESS ? core::Ok() : RegistryFailure(L"Writing context menu", result);
}

core::Status InstallEntry(std::wstring_view key_path, std::wstring_view executable,
                          std::wstring_view placeholder) {
  HKEY key = nullptr;
  LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, std::wstring(key_path).c_str(), 0,
                                   nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
  if (result != ERROR_SUCCESS) return RegistryFailure(L"Creating context menu", result);
  core::Status status = SetText(key, nullptr, L"Open with DHEPZ");
  if (status.ok()) status = SetText(key, L"Icon", executable);
  HKEY command = nullptr;
  if (status.ok()) {
    result = RegCreateKeyExW(key, L"command", 0, nullptr, 0, KEY_WRITE, nullptr,
                             &command, nullptr);
    if (result != ERROR_SUCCESS) status = RegistryFailure(L"Creating context command", result);
  }
  if (status.ok()) {
    const std::wstring value = str::QuoteArg(executable) + L" --path \"" +
                               std::wstring(placeholder) + L"\"";
    status = SetText(command, nullptr, value);
  }
  if (command != nullptr) RegCloseKey(command);
  RegCloseKey(key);
  return status;
}

}  // namespace

core::Status Install(std::wstring_view executable) {
  if (executable.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Context menu requires an executable path");
  }
  DHEPZ_RETURN_IF_ERROR(InstallEntry(kDirectoryKey, executable, L"%1"));
  return InstallEntry(kBackgroundKey, executable, L"%V");
}

core::Status Remove() {
  const LSTATUS directory = RegDeleteTreeW(HKEY_CURRENT_USER, kDirectoryKey);
  const LSTATUS background = RegDeleteTreeW(HKEY_CURRENT_USER, kBackgroundKey);
  if (directory != ERROR_SUCCESS && directory != ERROR_FILE_NOT_FOUND) {
    return RegistryFailure(L"Removing folder context menu", directory);
  }
  if (background != ERROR_SUCCESS && background != ERROR_FILE_NOT_FOUND) {
    return RegistryFailure(L"Removing background context menu", background);
  }
  return core::Ok();
}

}  // namespace explorer_context_menu

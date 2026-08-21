#include "modules/terminal/terminal_state.h"

#include <windows.h>

#include "core/json.h"

namespace terminal {
namespace {

constexpr std::wstring_view kRecentKey = L"recent_folders";
constexpr std::size_t kRecentCap = 10;

bool FileExists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

void RecentFolders::Add(std::wstring folder) {
  std::vector<std::wstring> next;
  next.push_back(folder);
  for (const std::wstring& existing : folders_) {
    if (existing != folder) next.push_back(existing);
    if (next.size() >= kRecentCap) break;
  }
  folders_ = std::move(next);
}

void RecentFolders::Remove(const std::wstring& folder) {
  for (std::size_t i = 0; i < folders_.size(); ++i) {
    if (folders_[i] == folder) {
      folders_.erase(folders_.begin() + static_cast<std::ptrdiff_t>(i));
      return;
    }
  }
}

void RecentFolders::Load(modules::ModuleHost& host) {
  std::wstring stored;
  if (!host.SettingsRead(kRecentKey, &stored).ok() || stored.empty()) return;
  json::Value parsed;
  if (!json::Parse(stored, &parsed).ok() || !parsed.is_array()) return;
  folders_.clear();
  for (const json::Value& item : parsed.items()) {
    if (item.is_string()) folders_.push_back(item.AsString());
  }
}

void RecentFolders::Save(modules::ModuleHost& host) const {
  json::Value array = json::Value::Array();
  for (const std::wstring& folder : folders_) {
    array.Append(json::Value::String(folder));
  }
  const core::Status saved = host.SettingsWrite(kRecentKey, json::Serialize(array, false));
  (void)saved;  // persistence is best-effort; a failed write keeps the session list
}

bool DetectVenv(const std::wstring& folder) {
  return FileExists(folder + L"\\Scripts\\activate.bat") ||
         FileExists(folder + L"\\bin\\activate");
}

std::wstring VenvActivatePath(const std::wstring& folder) {
  if (FileExists(folder + L"\\Scripts\\activate.bat")) {
    return folder + L"\\Scripts\\activate.bat";
  }
  return folder + L"\\bin\\activate";
}

core::Status ValidateFolder(const std::wstring& folder) {
  const DWORD attributes = GetFileAttributesW(folder.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return core::Err(core::ErrorCode::NotFound,
                     L"folder does not exist: " + folder);
  }
  if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"not a directory: " + folder);
  }
  return core::Ok();
}

}  // namespace terminal

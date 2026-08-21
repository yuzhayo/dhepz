#include "modules/terminal/terminal_state.h"

#include "core/json.h"

namespace terminal {
namespace {

constexpr std::wstring_view kRecentKey = L"recent_folders";
constexpr std::size_t kRecentCap = 10;
constexpr std::wstring_view kVenvEnabledKey = L"venv_enabled";

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

core::Status RecentFolders::Save(modules::ModuleHost& host) const {
  json::Value array = json::Value::Array();
  for (const std::wstring& folder : folders_) {
    array.Append(json::Value::String(folder));
  }
  return host.SettingsWrite(kRecentKey, json::Serialize(array, false));
}

bool LoadVenvPreference(modules::ModuleHost& host) {
  std::wstring stored;
  return host.SettingsRead(kVenvEnabledKey, &stored).ok() && stored == L"true";
}

core::Status SaveVenvPreference(modules::ModuleHost& host, bool enabled) {
  return host.SettingsWrite(kVenvEnabledKey, enabled ? L"true" : L"false");
}

}  // namespace terminal

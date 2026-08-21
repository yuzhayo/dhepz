// Pure terminal history state. Physical persistence is parent-owned and
// reached only through the module's narrowed settings section.
#pragma once

#include <string>
#include <vector>

#include "core/status.h"
#include "modules/contract/module_contract.h"

namespace terminal {

// Most-recent-first, deduped, capped at 10. Persists as a JSON array under
// "recent_folders" in the module's own settings section.
class RecentFolders final {
 public:
  void Add(std::wstring folder);
  void Remove(const std::wstring& folder);
  const std::vector<std::wstring>& List() const { return folders_; }

  void Load(modules::ModuleHost& host);
  core::Status Save(modules::ModuleHost& host) const;

 private:
  std::vector<std::wstring> folders_;
};

bool LoadVenvPreference(modules::ModuleHost& host);
core::Status SaveVenvPreference(modules::ModuleHost& host, bool enabled);

}  // namespace terminal

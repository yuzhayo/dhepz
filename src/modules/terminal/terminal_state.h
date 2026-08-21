// Terminal module state (P4-04): recent-folder history persisted in the
// module's OWN settings section (default reach), venv detection/toggle,
// folder validation. Filesystem probes are blocking — callers run them on
// a worker (P4-05).
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
  void Save(modules::ModuleHost& host) const;

 private:
  std::vector<std::wstring> folders_;
};

// Standard virtualenv layouts: <folder>/Scripts/activate.bat (Windows) or
// <folder>/bin/activate (posix-style checkout). Pure filesystem probes.
bool DetectVenv(const std::wstring& folder);
std::wstring VenvActivatePath(const std::wstring& folder);

// Exists + is a directory; human-friendly Status on failure.
core::Status ValidateFolder(const std::wstring& folder);

}  // namespace terminal

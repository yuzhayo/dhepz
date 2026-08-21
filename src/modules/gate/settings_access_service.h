// Parent-owned settings view used by ModuleHost capability facets. Physical
// persistence replaces this in IC-03 without widening the child contract.
#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "modules/contract/module_contract.h"

namespace modules {

class SettingsAccessService final {
 public:
  using PeersProvider = std::function<std::vector<PeerInfo>()>;

  explicit SettingsAccessService(PeersProvider peers_provider);

  core::Status ReadGlobal(std::wstring_view key, std::wstring* out);
  core::Status WriteGlobal(std::wstring_view key, std::wstring_view value);
  core::Status ReadModule(std::wstring_view module_id, std::wstring_view key,
                          std::wstring* out);
  core::Status WriteModule(std::wstring_view module_id, std::wstring_view key,
                           std::wstring_view value);
  std::vector<PeerInfo> Peers() const;

 private:
  PeersProvider peers_provider_;
  mutable std::mutex mutex_;
  std::map<std::wstring, std::map<std::wstring, std::wstring>> modules_;
  std::map<std::wstring, std::wstring> global_;
};

}  // namespace modules

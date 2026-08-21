// Parent-owned settings view used by ModuleHost capability facets. It owns the
// lazy physical store without exposing paths or file APIs to child modules.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "modules/contract/module_contract.h"
#include "modules/gate/settings_store.h"

namespace modules {

class SettingsAccessService final {
 public:
  using PeersProvider = std::function<std::vector<PeerInfo>()>;

  explicit SettingsAccessService(PeersProvider peers_provider);
  ~SettingsAccessService();

  core::Status ConfigureHost(void* ui_window,
                             unsigned int completion_message);
  core::Status ConfigureStorePath(std::wstring path);
  core::Status StartLoad(HostOperationCallback callback,
                         AsyncRequestToken* token);
  void CancelRequest(AsyncRequestToken token);

  core::Status ReadGlobal(std::wstring_view key, std::wstring* out);
  core::Status WriteGlobal(std::wstring_view key, std::wstring_view value);
  core::Status ReadModule(std::wstring_view module_id, std::wstring_view key,
                          std::wstring* out);
  core::Status WriteModule(std::wstring_view module_id, std::wstring_view key,
                           std::wstring_view value);
  std::vector<PeerInfo> Peers() const;
  const std::vector<SettingsStoreDiagnostic>& Diagnostics() const;
  std::size_t ThreadCount();

 private:
  SettingsStore* EnsureStore();

  PeersProvider peers_provider_;
  void* ui_window_ = nullptr;
  unsigned int completion_message_ = 0;
  std::wstring store_path_;
  std::unique_ptr<SettingsStore> store_;
};

}  // namespace modules

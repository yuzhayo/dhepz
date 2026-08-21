#include "modules/gate/settings_access_service.h"

namespace modules {

SettingsAccessService::SettingsAccessService(PeersProvider peers_provider)
    : peers_provider_(std::move(peers_provider)) {}

core::Status SettingsAccessService::ReadGlobal(std::wstring_view key,
                                               std::wstring* out) {
  if (out == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"settings output is required");
  }
  std::lock_guard lock(mutex_);
  const auto it = global_.find(std::wstring(key));
  if (it == global_.end()) {
    return core::Err(core::ErrorCode::NotFound, L"no such key");
  }
  *out = it->second;
  return core::Ok();
}

core::Status SettingsAccessService::WriteGlobal(std::wstring_view key,
                                                std::wstring_view value) {
  std::lock_guard lock(mutex_);
  global_[std::wstring(key)] = std::wstring(value);
  return core::Ok();
}

core::Status SettingsAccessService::ReadModule(std::wstring_view module_id,
                                               std::wstring_view key,
                                               std::wstring* out) {
  if (out == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"settings output is required");
  }
  std::lock_guard lock(mutex_);
  const auto section = modules_.find(std::wstring(module_id));
  if (section == modules_.end()) {
    return core::Err(core::ErrorCode::NotFound, L"no such module settings");
  }
  const auto value = section->second.find(std::wstring(key));
  if (value == section->second.end()) {
    return core::Err(core::ErrorCode::NotFound, L"no such key");
  }
  *out = value->second;
  return core::Ok();
}

core::Status SettingsAccessService::WriteModule(std::wstring_view module_id,
                                                std::wstring_view key,
                                                std::wstring_view value) {
  std::lock_guard lock(mutex_);
  modules_[std::wstring(module_id)][std::wstring(key)] = std::wstring(value);
  return core::Ok();
}

std::vector<PeerInfo> SettingsAccessService::Peers() const {
  return peers_provider_ ? peers_provider_() : std::vector<PeerInfo>{};
}

}  // namespace modules

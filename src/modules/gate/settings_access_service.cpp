#include "modules/gate/settings_access_service.h"

#include <utility>

namespace modules {

SettingsAccessService::SettingsAccessService(PeersProvider peers_provider)
    : peers_provider_(std::move(peers_provider)) {}

SettingsAccessService::~SettingsAccessService() = default;

core::Status SettingsAccessService::ConfigureHost(
    void* ui_window, unsigned int completion_message) {
  if (store_) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"settings store is already active");
  }
  if (ui_window == nullptr || completion_message == 0) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"settings host requires a UI completion target");
  }
  ui_window_ = ui_window;
  completion_message_ = completion_message;
  return core::Ok();
}

core::Status SettingsAccessService::ConfigureStorePath(std::wstring path) {
  if (store_) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"settings store is already active");
  }
  store_path_ = std::move(path);
  return core::Ok();
}

SettingsStore* SettingsAccessService::EnsureStore() {
  if (!store_) {
    store_ = std::make_unique<SettingsStore>(
        ui_window_, completion_message_, store_path_);
  }
  return store_.get();
}

core::Status SettingsAccessService::StartLoad(
    HostOperationCallback callback, AsyncRequestToken* token) {
  if (!callback) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"settings-ready callback is required");
  }
  if (ui_window_ == nullptr || completion_message_ == 0) {
    return core::Err(core::ErrorCode::Unsupported,
                     L"async settings host is not configured");
  }
  return EnsureStore()->StartLoad(
      [callback = std::move(callback)](
          const SettingsStoreCompletion& completion) {
        HostOperationCompletion host_completion;
        host_completion.token = completion.token;
        host_completion.kind = completion.operation == SettingsStoreOperation::Load
                                   ? HostOperationKind::SettingsLoad
                                   : HostOperationKind::SettingsSave;
        host_completion.status = completion.status;
        callback(host_completion);
      },
      token);
}

void SettingsAccessService::CancelRequest(AsyncRequestToken token) {
  if (store_) store_->CancelRequest(token);
}

core::Status SettingsAccessService::ReadGlobal(std::wstring_view key,
                                               std::wstring* out) {
  if (out == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"settings output is required");
  }
  if (!store_) {
    out->clear();
    return core::Err(core::ErrorCode::NotFound,
                     L"settings are not loaded");
  }
  return store_->ReadGlobal(key, out);
}

core::Status SettingsAccessService::WriteGlobal(std::wstring_view key,
                                                std::wstring_view value) {
  if (ui_window_ == nullptr || completion_message_ == 0) {
    return core::Err(core::ErrorCode::Unsupported,
                     L"async settings host is not configured");
  }
  AsyncRequestToken token;
  return EnsureStore()->StartWriteGlobal(
      key, value, [](const SettingsStoreCompletion&) {}, &token);
}

core::Status SettingsAccessService::ReadModule(std::wstring_view module_id,
                                               std::wstring_view key,
                                               std::wstring* out) {
  if (out == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"settings output is required");
  }
  if (!store_) {
    out->clear();
    return core::Err(core::ErrorCode::NotFound,
                     L"settings are not loaded");
  }
  return store_->ReadModule(module_id, key, out);
}

core::Status SettingsAccessService::WriteModule(std::wstring_view module_id,
                                                std::wstring_view key,
                                                std::wstring_view value) {
  if (ui_window_ == nullptr || completion_message_ == 0) {
    return core::Err(core::ErrorCode::Unsupported,
                     L"async settings host is not configured");
  }
  AsyncRequestToken token;
  return EnsureStore()->StartWriteModule(
      module_id, key, value, [](const SettingsStoreCompletion&) {}, &token);
}

std::vector<PeerInfo> SettingsAccessService::Peers() const {
  return peers_provider_ ? peers_provider_() : std::vector<PeerInfo>{};
}

const std::vector<SettingsStoreDiagnostic>&
SettingsAccessService::Diagnostics() const {
  static const std::vector<SettingsStoreDiagnostic> empty;
  return store_ ? store_->diagnostics() : empty;
}

std::size_t SettingsAccessService::ThreadCount() {
  if (!store_) return 0;
  store_->ReapFinished();
  return store_->ThreadCount();
}

}  // namespace modules

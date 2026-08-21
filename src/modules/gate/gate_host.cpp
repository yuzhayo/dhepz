#include "modules/gate/gate_host.h"

#include <utility>

#include "modules/gate/host_operation_dispatcher.h"

namespace modules {

GateHost::GateHost(std::wstring module_id,
                   HostRouteRequestHandler route_request_handler,
                   HostPeersHandler peers_handler, void* operation_window,
                   unsigned int operation_message,
                   HostStatePatchHandler state_patch_handler)
    : module_id_(std::move(module_id)),
      route_request_handler_(std::move(route_request_handler)),
      peers_handler_(std::move(peers_handler)) {
  if (operation_window == nullptr || operation_message == 0) return;

  StatePatchSink sink;
  if (state_patch_handler) {
    sink = [module_id = module_id_, handler = std::move(state_patch_handler)](
               const json::Value& patch) { return handler(module_id, patch); };
  }
  operations_ = std::make_unique<HostOperationDispatcher>(
      operation_window, operation_message, std::move(sink));
}

GateHost::~GateHost() = default;

ModuleSurface GateHost::Surface() { return surface_; }

core::Status GateHost::SettingsRead(std::wstring_view key, std::wstring* out) {
  std::lock_guard lock(mutex_);
  const auto& section = settings_[module_id_];
  const auto it = section.find(std::wstring(key));
  if (it == section.end()) {
    return core::Err(core::ErrorCode::NotFound, L"no such key");
  }
  *out = it->second;
  return core::Ok();
}

core::Status GateHost::SettingsReadGlobal(std::wstring_view key,
                                          std::wstring* out) {
  std::lock_guard lock(mutex_);
  const auto it = global_.find(std::wstring(key));
  if (it == global_.end()) {
    return core::Err(core::ErrorCode::NotFound, L"no such key");
  }
  *out = it->second;
  return core::Ok();
}

core::Status GateHost::SettingsWrite(std::wstring_view key,
                                     std::wstring_view value) {
  std::lock_guard lock(mutex_);
  settings_[module_id_][std::wstring(key)] = std::wstring(value);
  return core::Ok();
}

core::Status GateHost::StorageWrite(std::wstring_view name,
                                    std::wstring_view data) {
  std::lock_guard lock(mutex_);
  storage_[std::wstring(name)] = std::wstring(data);
  return core::Ok();
}

core::Status GateHost::StorageRead(std::wstring_view name, std::wstring* out) {
  std::lock_guard lock(mutex_);
  const auto it = storage_.find(std::wstring(name));
  if (it == storage_.end()) {
    return core::Err(core::ErrorCode::NotFound, L"no such blob");
  }
  *out = it->second;
  return core::Ok();
}

core::Status GateHost::StartProcess(const ProcessRequest& request,
                                    HostOperationCallback callback,
                                    AsyncRequestToken* token) {
  if (!operations_) {
    return core::Err(core::ErrorCode::Unsupported,
                     L"async process host is not configured");
  }
  return operations_->StartProcess(request, std::move(callback), token);
}

core::Status GateHost::StartFolderProbe(const FolderProbeRequest& request,
                                        HostOperationCallback callback,
                                        AsyncRequestToken* token) {
  if (!operations_) {
    return core::Err(core::ErrorCode::Unsupported,
                     L"async folder host is not configured");
  }
  return operations_->StartFolderProbe(request, std::move(callback), token);
}

void GateHost::CancelRequest(AsyncRequestToken token) {
  if (operations_) operations_->CancelRequest(token);
}

core::Status GateHost::PublishStatePatch(const json::Value& patch) {
  if (!operations_) {
    return core::Err(core::ErrorCode::Unsupported,
                     L"state patch sink is not configured");
  }
  return operations_->PublishStatePatch(patch);
}

void GateHost::ReportStatus(const core::Status& status) { last_status_ = status; }

void GateHost::Log(std::wstring_view level, std::wstring_view text) {
  std::lock_guard lock(mutex_);
  log_.push_back(std::wstring(level) + L": " + std::wstring(text));
}

core::Status GateHost::RequestRoute(std::wstring_view route_id) {
  if (!route_request_handler_) {
    return core::Err(core::ErrorCode::Unsupported,
                     L"route requests are not configured");
  }
  return route_request_handler_(route_id);
}

std::vector<PeerInfo> GateHost::Peers() {
  return peers_handler_ ? peers_handler_() : std::vector<PeerInfo>{};
}

}  // namespace modules

#include "modules/gate/gate_host.h"

#include <utility>

#include "modules/gate/config_transaction_service.h"
#include "modules/gate/host_operation_dispatcher.h"
#include "modules/gate/settings_access_service.h"

namespace modules {

GateHost::GateHost(std::wstring module_id,
                   HostRouteRequestHandler route_request_handler,
                   HostPeersHandler peers_handler,
                   HostDiagnosticsProvider diagnostics_provider,
                   HostStatusHandler status_handler,
                   SettingsAccessService* settings_service,
                   ConfigTransactionService* config_service,
                   bool settings_all_granted, bool config_write_granted,
                   void* operation_window,
                   unsigned int operation_message,
                   HostStatePatchHandler state_patch_handler)
    : module_id_(std::move(module_id)),
      route_request_handler_(std::move(route_request_handler)),
      peers_handler_(std::move(peers_handler)),
      diagnostics_provider_(std::move(diagnostics_provider)),
      status_handler_(std::move(status_handler)),
      settings_service_(settings_service),
      config_service_(config_service),
      settings_all_granted_(settings_all_granted),
      config_write_granted_(config_write_granted) {
  if (operation_window == nullptr || operation_message == 0) return;

  StatePatchSink sink;
  if (state_patch_handler) {
    sink = [module_id = module_id_, handler = std::move(state_patch_handler)](
               const json::Value& patch) { return handler(module_id, patch); };
  }
  operations_ = std::make_unique<HostOperationDispatcher>(
      operation_window, operation_message, std::move(sink));
}

GateHost::~GateHost() { lifetime_alive_->store(false); }

ModuleSurface GateHost::Surface() { return surface_; }

core::Status GateHost::SettingsRead(std::wstring_view key, std::wstring* out) {
  return settings_service_->ReadModule(module_id_, key, out);
}

core::Status GateHost::SettingsReadGlobal(std::wstring_view key,
                                          std::wstring* out) {
  return settings_service_->ReadGlobal(key, out);
}

core::Status GateHost::SettingsWrite(std::wstring_view key,
                                     std::wstring_view value) {
  return settings_service_->WriteModule(module_id_, key, value);
}

core::Status GateHost::StartSettingsLoad(HostOperationCallback callback,
                                         AsyncRequestToken* token) {
  if (!callback) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"settings-ready callback is required");
  }
  const std::shared_ptr<std::atomic<bool>> alive = lifetime_alive_;
  return settings_service_->StartLoad(
      [alive, callback = std::move(callback)](
          const HostOperationCompletion& completion) {
        if (alive->load()) callback(completion);
      },
      token);
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
  settings_service_->CancelRequest(token);
}

core::Status GateHost::PublishStatePatch(const json::Value& patch) {
  if (!operations_) {
    return core::Err(core::ErrorCode::Unsupported,
                     L"state patch sink is not configured");
  }
  return operations_->PublishStatePatch(patch);
}

core::Status GateHost::GetSettingsAllFacet(SettingsAllFacet** facet) {
  if (facet == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"facet output is required");
  }
  *facet = settings_all_granted_ ? this : nullptr;
  return *facet != nullptr
             ? core::Ok()
             : core::Err(core::ErrorCode::PermissionDenied,
                         L"settings:all is not granted");
}

core::Status GateHost::GetConfigWriteFacet(ConfigWriteFacet** facet) {
  if (facet == nullptr) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"facet output is required");
  }
  *facet = config_write_granted_ ? this : nullptr;
  return *facet != nullptr
             ? core::Ok()
             : core::Err(core::ErrorCode::PermissionDenied,
                         L"config:write is not granted");
}

DiagnosticsReadModel GateHost::Diagnostics() {
  return diagnostics_provider_ ? diagnostics_provider_() : DiagnosticsReadModel{};
}

core::Status GateHost::ReadGlobal(std::wstring_view key, std::wstring* out) {
  return settings_service_->ReadGlobal(key, out);
}

core::Status GateHost::WriteGlobal(std::wstring_view key,
                                   std::wstring_view value) {
  return settings_service_->WriteGlobal(key, value);
}

core::Status GateHost::ReadModule(std::wstring_view module_id,
                                  std::wstring_view key,
                                  std::wstring* out) {
  return settings_service_->ReadModule(module_id, key, out);
}

core::Status GateHost::WriteModule(std::wstring_view module_id,
                                   std::wstring_view key,
                                   std::wstring_view value) {
  return settings_service_->WriteModule(module_id, key, value);
}

core::Status GateHost::Preview(std::wstring_view candidate,
                               ConfigPreviewResult* result) {
  return config_service_->Preview(candidate, result);
}

core::Status GateHost::Save(ConfigPreviewToken preview,
                            HostOperationCallback callback,
                            AsyncRequestToken* request) {
  if (!operations_) {
    return core::Err(core::ErrorCode::Unsupported,
                     L"async config save host is not configured");
  }
  std::wstring path;
  std::wstring text;
  DHEPZ_RETURN_IF_ERROR(config_service_->BeginSave(preview, &path, &text));
  const core::Status started = operations_->StartAtomicWrite(
      std::move(path), std::move(text),
      [service = config_service_, preview, callback = std::move(callback)](
          const HostOperationCompletion& completion) {
        service->CompleteSave(preview, completion.status);
        callback(completion);
      },
      request);
  if (!started.ok()) {
    const core::Status aborted = config_service_->AbortSave(preview);
    (void)aborted;
  }
  return started;
}

core::Status GateHost::Discard(
    ConfigPreviewToken preview,
    std::vector<std::wstring>* affected_routes) {
  return config_service_->Discard(preview, affected_routes);
}

void GateHost::ReportStatus(const core::Status& status) {
  last_status_ = status;
  if (status_handler_) status_handler_(module_id_, status);
}

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

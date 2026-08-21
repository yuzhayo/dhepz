#include "modules/gate/app_gate.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "modules/gate/config_transaction_service.h"
#include "modules/gate/gate_host.h"
#include "modules/gate/gate_startup_service.h"
#include "modules/gate/settings_access_service.h"
#include "modules/registry/module_registry.h"

namespace modules {
AppGate::AppGate()
    : settings_service_(
          std::make_unique<SettingsAccessService>([this]() { return Peers(); })),
      config_service_(std::make_unique<ConfigTransactionService>(
          &document_, &document_generation_)) {}

AppGate::~AppGate() { Shutdown(); }

core::Status AppGate::Start(std::wstring_view override_path) {
  return StartFromResource(L"EMBEDDED_UI", override_path);
}

core::Status AppGate::StartFromResource(std::wstring_view resource_name,
                                        std::wstring_view override_path) {
  if (shutdown_) {
    return core::Err(core::ErrorCode::Cancelled, L"AppGate is shut down");
  }
  if (start_pending_ || document_ != nullptr || !mounted_.empty()) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"AppGate was already started");
  }
  std::wstring embedded;
  DHEPZ_RETURN_IF_ERROR(
      GateStartupService::ReadEmbeddedResource(resource_name, &embedded));
  if (override_path.empty()) {
    start_status_ = PairAndMount(embedded, {});
    return start_status_;
  }
  if (operation_window_ == nullptr || operation_message_ == 0) {
    return core::Err(core::ErrorCode::Unsupported,
                     L"Config override requires the parent worker path");
  }

  start_pending_ = true;
  startup_service_ = std::make_unique<GateStartupService>(operation_window_,
                                                          operation_message_);
  const std::wstring source(override_path);
  return startup_service_->ReadOverride(
      source,
      [this, embedded = std::move(embedded), source](core::Status read_status,
                                                     std::wstring override_text) {
        if (read_status.ok()) {
          start_status_ = PairAndMount(embedded, override_text);
          if (!start_status_.ok()) {
            const core::Status invalid_override = start_status_;
            start_status_ = PairAndMount(embedded, {});
            rejects_.push_back({L"config-override", invalid_override.Message(),
                                source, 1, 1, DiagnosticStage::Startup});
          }
        } else {
          start_status_ = PairAndMount(embedded, {});
          rejects_.push_back(
              {L"config-override", read_status.Message(), source, 1, 1,
               DiagnosticStage::Startup});
        }
        start_pending_ = false;
      });
}

core::Status AppGate::StartWithEmbedded(std::wstring_view embedded_text,
                                        std::wstring_view override_text) {
  if (shutdown_) {
    return core::Err(core::ErrorCode::Cancelled, L"AppGate is shut down");
  }
  if (start_pending_ || document_ != nullptr || !mounted_.empty()) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"AppGate was already started");
  }
  return PairAndMount(embedded_text, override_text);
}

core::Status AppGate::ConfigureHostOperations(
    void* ui_window, unsigned int completion_message,
    HostStatePatchHandler state_patch_handler) {
  if (start_pending_ || document_ != nullptr || !mounted_.empty()) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"Host operations must be configured before gate start");
  }
  if (ui_window == nullptr || completion_message == 0) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"Host operations require a UI window and completion message");
  }
  operation_window_ = ui_window;
  operation_message_ = completion_message;
  state_patch_handler_ = std::move(state_patch_handler);
  return settings_service_->ConfigureHost(ui_window, completion_message);
}

core::Status AppGate::ConfigureConfigOverridePath(std::wstring path) {
  if (start_pending_ || document_ != nullptr || !mounted_.empty()) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"Config path must be configured before gate start");
  }
  return config_service_->ConfigureOverridePath(std::move(path));
}

core::Status AppGate::ConfigureSettingsStorePath(std::wstring path) {
  if (start_pending_ || document_ != nullptr || !mounted_.empty()) {
    return core::Err(core::ErrorCode::AlreadyExists,
                     L"Settings path must be configured before gate start");
  }
  return settings_service_->ConfigureStorePath(std::move(path));
}

const std::vector<SettingsStoreDiagnostic>&
AppGate::SettingsDiagnostics() const {
  return settings_service_->Diagnostics();
}

core::Status AppGate::PairAndMount(std::wstring_view embedded_text,
                                   std::wstring_view override_text) {
  ModuleValidationResult validation;
  ModuleValidator validator;
  const core::Status status = validator.Validate(
      embedded_text, override_text, CollectModules(), &validation);
  if (!status.ok()) return status;

  document_ = std::move(validation.document);
  core_catalog_ = std::move(validation.core_catalog);
  live_sources_ = std::move(validation.accepted_sources);
  rejects_ = std::move(validation.rejects);
  grants_ = std::move(validation.grants);
  runtime_faults_.clear();
  module_statuses_.clear();
  action_map_ = std::move(validation.action_map);
  mounted_.clear();
  current_route_ = document_ != nullptr ? document_->initial_route() : L"";
  ++document_generation_;
  config_service_->ConfigureBase(core_catalog_,
                                 validation.accepted_base);

  for (ValidatedModule& accepted : validation.modules) {
    MountedModule module;
    module.manifest = std::move(accepted.manifest);
    module.descriptor = std::move(accepted.descriptor);
    module.settings_all_granted = accepted.settings_all_granted;
    module.config_write_granted = accepted.config_write_granted;
    mounted_.push_back(std::move(module));
  }
  return core::Ok();
}

AppGate::MountedModule* AppGate::FindByRoute(std::wstring_view route_id) {
  if (document_ == nullptr) return nullptr;
  const ui::config::Route* route = document_->FindRoute(route_id);
  if (route == nullptr) return nullptr;
  return FindByModule(route->root.GetString(L"module_id"));
}

AppGate::MountedModule* AppGate::FindByModule(std::wstring_view module_id) {
  for (MountedModule& module : mounted_) {
    if (module.manifest.module_id == module_id && !module.quarantined) {
      return &module;
    }
  }
  return nullptr;
}

bool AppGate::Mounted(std::wstring_view module_id) const {
  for (const MountedModule& module : mounted_) {
    if (module.manifest.module_id == module_id && !module.quarantined) return true;
  }
  return false;
}

core::Status AppGate::Activate(std::wstring_view route_id) {
  MountedModule* module = FindByRoute(route_id);
  if (module == nullptr) return core::Ok();
  return BindModule(module);
}

core::Status AppGate::Dispatch(std::wstring_view action,
                               const json::Value& payload,
                               json::Value* state_patch) {
  for (const auto& [registered_action, module_id] : action_map_) {
    if (registered_action != action) continue;
    MountedModule* module = FindByModule(module_id);
    if (module == nullptr) {
      return core::Err(core::ErrorCode::NotFound, L"module not mounted");
    }
    if (!module->activated) {
      const core::Status bound = BindModule(module);
      if (!bound.ok()) return bound;
    }
    core::Status handled;
    try {
      handled = module->descriptor->Handle(action, payload, state_patch);
    } catch (const std::exception&) {
      handled = core::Err(core::ErrorCode::Internal,
                          L"module Handle threw an exception");
    } catch (...) {
      handled = core::Err(core::ErrorCode::Internal,
                          L"module Handle threw an unknown exception");
    }
    ReportModuleStatus(module_id, handled);
    if (handled.Code() == core::ErrorCode::Internal) {
      Quarantine(module, DiagnosticStage::Dispatch,
                 L"fatal module result: " + handled.Message());
    }
    return handled;
  }
  return core::Err(core::ErrorCode::NotFound,
                   L"no module declares this action");
}

core::Status AppGate::RequestRoute(std::wstring_view route_id) {
  if (document_ == nullptr || document_->FindRoute(route_id) == nullptr) {
    return core::Err(core::ErrorCode::NotFound, L"unknown route");
  }
  current_route_ = std::wstring(route_id);
  return Activate(route_id);
}

std::vector<PeerInfo> AppGate::Peers() const {
  std::vector<PeerInfo> peers;
  for (const MountedModule& module : mounted_) {
    if (module.quarantined) continue;
    peers.push_back({module.manifest.module_id, module.manifest.tab_label,
                     module.manifest.settings_route});
  }
  return peers;
}

core::Status AppGate::BindModule(MountedModule* module) {
  if (module == nullptr || module->quarantined) {
    return core::Err(core::ErrorCode::NotFound, L"module is unavailable");
  }
  if (module->activated) return core::Ok();
  EnsureHost(module);
  module->bound_lifetime_started = true;
  core::Status bound;
  try {
    bound = module->descriptor->Bind(*module->host);
  } catch (const std::exception&) {
    bound = core::Err(core::ErrorCode::Internal,
                      L"module Bind threw an exception");
  } catch (...) {
    bound = core::Err(core::ErrorCode::Internal,
                      L"module Bind threw an unknown exception");
  }
  ReportModuleStatus(module->manifest.module_id, bound);
  if (!bound.ok()) {
    Quarantine(module, DiagnosticStage::Bind,
               L"Bind failed: " + bound.Message());
    return bound;
  }
  module->activated = true;
  return core::Ok();
}

void AppGate::EnsureHost(MountedModule* module) {
  if (module == nullptr || module->host != nullptr) return;
  module->host = std::make_unique<GateHost>(
      module->manifest.module_id,
      [this](std::wstring_view route_id) { return RequestRoute(route_id); },
      [this]() { return Peers(); }, [this]() { return Diagnostics(); },
      [this](std::wstring_view module_id, const core::Status& status) {
        ReportModuleStatus(module_id, status);
      },
      settings_service_.get(), config_service_.get(),
      module->settings_all_granted, module->config_write_granted,
      operation_window_, operation_message_, state_patch_handler_);
}

void AppGate::ReleaseBoundModule(MountedModule* module) {
  if (module == nullptr) return;
  if (module->bound_lifetime_started) {
    module->bound_lifetime_started = false;
    try {
      module->descriptor->Release();
    } catch (...) {
      runtime_faults_.push_back(
          {module->manifest.module_id, L"module Release threw an exception",
           L"runtime", 1, 1, DiagnosticStage::Runtime});
    }
  }
  module->activated = false;
  // Destroying the host invalidates its worker generation and suppresses
  // settings completions captured by this host lifetime.
  module->host.reset();
}

void AppGate::Quarantine(MountedModule* module, DiagnosticStage stage,
                         std::wstring reason) {
  if (module == nullptr || module->quarantined) return;
  const std::wstring module_id = module->manifest.module_id;
  runtime_faults_.push_back(
      {module_id, std::move(reason), L"runtime", 1, 1, stage});
  ReleaseBoundModule(module);
  module->quarantined = true;
  std::erase_if(action_map_, [&module_id](const auto& action) {
    return action.second == module_id;
  });

  ModuleValidator validator;
  std::vector<ui::config::ScreenSource> filtered;
  std::unique_ptr<ui::config::ResolvedUiDocument> filtered_document;
  if (validator.WithdrawModule(core_catalog_, live_sources_, module_id,
                               &filtered, &filtered_document)
          .ok()) {
    live_sources_ = std::move(filtered);
    document_ = std::move(filtered_document);
    ++document_generation_;
    if (document_ != nullptr &&
        document_->FindRoute(current_route_) == nullptr) {
      current_route_ = document_->initial_route();
    }
  }
}

void AppGate::ReportModuleStatus(std::wstring_view module_id,
                                 const core::Status& status) {
  for (ModuleStatusEntry& entry : module_statuses_) {
    if (entry.module_id == module_id) {
      entry.ok = status.ok();
      entry.message = status.Message();
      return;
    }
  }
  module_statuses_.push_back(
      {std::wstring(module_id), status.ok(), status.Message()});
}

DiagnosticsReadModel AppGate::Diagnostics() const {
  DiagnosticsReadModel model;
  model.accepted = Peers();
  model.rejected = rejects_;
  model.grants = grants_;
  model.runtime_faults = runtime_faults_;
  model.statuses = module_statuses_;
  return model;
}

void AppGate::ReleaseWindowModules() {
  if (shutdown_) return;
  for (MountedModule& module : mounted_) {
    if (!module.quarantined) ReleaseBoundModule(&module);
  }
}

void AppGate::Shutdown() {
  if (shutdown_) return;
  shutdown_ = true;
  start_pending_ = false;
  startup_service_.reset();
  for (MountedModule& module : mounted_) ReleaseBoundModule(&module);
  action_map_.clear();
}

}  // namespace modules

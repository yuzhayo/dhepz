#include "modules/gate/app_gate.h"

#include <algorithm>

#include "core/json.h"
#include "modules/gate/gate_host.h"
#include "modules/registry/module_registry.h"
#include "ui/config/config_store.h"

namespace modules {
namespace {

std::vector<RegisteredModule> RegistrySnapshot() { return CollectModules(); }

const std::vector<RejectEntry>* g_rejects = nullptr;

}  // namespace

const std::vector<RejectEntry>& CurrentRejects() {
  static const std::vector<RejectEntry> empty;
  return g_rejects != nullptr ? *g_rejects : empty;
}

AppGate::AppGate() = default;
AppGate::~AppGate() = default;

core::Status AppGate::Start(std::wstring_view override_path) {
  // Embedded RCDATA read lands here once the resource is wired; tests and
  // the gate fixtures use StartWithEmbedded.
  (void)override_path;
  return core::Err(core::ErrorCode::Unsupported, L"embedded resource read lands with #84");
}

core::Status AppGate::StartWithEmbedded(std::wstring_view embedded_text,
                                        std::wstring_view override_text) {
  return PairAndMount(embedded_text, override_text);
}

core::Status AppGate::ConfigureHostOperations(void* ui_window,
                                              unsigned int completion_message,
                                              HostStatePatchHandler state_patch_handler) {
  if (!mounted_.empty()) {
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
  return core::Ok();
}

core::Status AppGate::PairAndMount(std::wstring_view embedded_text,
                                   std::wstring_view override_text) {
  json::Value embedded;
  DHEPZ_RETURN_IF_ERROR(json::Parse(embedded_text, &embedded));
  const json::Value* core = embedded.Find(L"core");
  if (core == nullptr || !core->is_object()) {
    return core::Err(core::ErrorCode::InvalidArgument,
                     L"embedded document carries no core catalog");
  }

  json::Value embedded_screens = json::Value::Object();
  if (const json::Value* components = embedded.Find(L"components")) {
    embedded_screens.Set(L"components", *components);
  }
  std::vector<ui::config::ScreenSource> sources;
  sources.push_back({L"embedded", json::Serialize(embedded_screens)});
  if (!override_text.empty()) {
    sources.push_back({L"override", std::wstring(override_text)});
  }
  std::vector<ui::config::Diagnostic> ui_diags;
  DHEPZ_RETURN_IF_ERROR(ui::config::ResolveDocument(*core, sources, &ui_diags, &document_));

  const std::vector<RegisteredModule> registered = RegistrySnapshot();
  const json::Value* manifests = embedded.Find(L"modules");
  int settings_all_claimants = 0;

  if (manifests != nullptr && manifests->is_array()) {
    for (const json::Value& manifest_value : manifests->items()) {
      ModuleManifest manifest;
      std::vector<ManifestDiagnostic> manifest_diags;
      if (!ParseManifest(json::Serialize(manifest_value), &manifest, &manifest_diags).ok()) {
        const json::Value* raw_id = manifest_value.Find(L"moduleId");
        const std::wstring id =
            (raw_id != nullptr && raw_id->is_string()) ? raw_id->AsString() : L"?";
        rejects_.push_back({id,
                            L"manifest invalid: " +
                                (manifest_diags.empty() ? L"?" : manifest_diags[0].message)});
        continue;
      }

      const ModuleFactory* factory = nullptr;
      for (const RegisteredModule& reg : registered) {
        if (reg.module_id == manifest.module_id) {
          factory = &reg.factory;
          break;
        }
      }
      if (factory == nullptr) {
        rejects_.push_back({manifest.module_id, L"logic half not self-registered"});
        continue;
      }

      // Pairing: a screen half with matching module_id must exist.
      const ui::config::Route* screen = nullptr;
      for (const ui::config::Route& route : document_->routes()) {
        if (route.root.GetString(L"module_id") == manifest.module_id) {
          screen = &route;
          break;
        }
      }
      if (screen == nullptr) {
        rejects_.push_back({manifest.module_id, L"no screen half with matching module_id"});
        continue;
      }

      std::unique_ptr<ModuleDescriptor> descriptor = (*factory)();
      const std::vector<std::wstring> declared = descriptor->DeclaredCapabilities();
      if (declared != manifest.capabilities) {
        rejects_.push_back({manifest.module_id,
                            L"DeclaredCapabilities() does not match module.json"});
        continue;
      }
      {
        const std::vector<std::wstring> code_actions = descriptor->DeclaredActions();
        std::vector<std::wstring> missing_in_code;
        for (const std::wstring& action : manifest.actions) {
          if (std::find(code_actions.begin(), code_actions.end(), action) ==
              code_actions.end()) {
            missing_in_code.push_back(action);
          }
        }
        if (!missing_in_code.empty()) {
          rejects_.push_back({manifest.module_id,
                              L"action '" + missing_in_code[0] +
                                  L"' declared in module.json has no handler in C++"});
          continue;
        }
      }
      bool capability_ok = true;
      for (const std::wstring& cap : declared) {
        if (cap == std::wstring(kCapabilitySettingsAll)) {
          if (++settings_all_claimants > 1) {
            rejects_.push_back({manifest.module_id,
                                L"settings:all already claimed by another module"});
            capability_ok = false;
            break;
          }
        } else if (cap != std::wstring(kCapabilityConfigWrite)) {
          rejects_.push_back({manifest.module_id, L"unknown capability '" + cap + L"'"});
          capability_ok = false;
          break;
        }
      }
      if (!capability_ok) continue;

      if (!manifest.settings_route.empty()) {
        bool ships = false;
        for (const ui::config::Route& route : document_->routes()) {
          if (route.id == manifest.settings_route &&
              route.root.GetString(L"module_id") == manifest.module_id) {
            ships = true;
            break;
          }
        }
        if (!ships) {
          rejects_.push_back({manifest.module_id,
                              L"settingsRoute not shipped by this module"});
          continue;
        }
      }

      MountedModule module;
      module.manifest = std::move(manifest);
      bool actions_ok = true;
      for (const std::wstring& action : descriptor->DeclaredActions()) {
        for (const auto& [existing_action, existing_id] : action_map_) {
          if (existing_action == action) {
            rejects_.push_back({module.manifest.module_id,
                                L"action '" + action + L"' already registered by " +
                                    existing_id});
            actions_ok = false;
            break;
          }
        }
        if (!actions_ok) break;
      }
      if (!actions_ok) continue;
      for (const std::wstring& action : descriptor->DeclaredActions()) {
        action_map_.emplace_back(action, module.manifest.module_id);
      }
      module.descriptor = std::move(descriptor);
      module.host = std::make_unique<GateHost>(
          module.manifest.module_id,
          [this](std::wstring_view route_id) { return RequestRoute(route_id); },
          [this]() { return Peers(); }, operation_window_, operation_message_,
          state_patch_handler_);
      mounted_.push_back(std::move(module));
    }
  }

  current_route_ = document_->initial_route();
  g_rejects = &rejects_;
  return core::Ok();
}

AppGate::MountedModule* AppGate::FindByRoute(std::wstring_view route_id) {
  for (MountedModule& module : mounted_) {
    if (document_ != nullptr) {
      const ui::config::Route* route = document_->FindRoute(route_id);
      if (route != nullptr && route->root.GetString(L"module_id") == module.manifest.module_id) {
        return &module;
      }
    }
  }
  return nullptr;
}

AppGate::MountedModule* AppGate::FindByModule(std::wstring_view module_id) {
  for (MountedModule& module : mounted_) {
    if (module.manifest.module_id == module_id) return &module;
  }
  return nullptr;
}

bool AppGate::Mounted(std::wstring_view module_id) const {
  for (const MountedModule& module : mounted_) {
    if (module.manifest.module_id == module_id) return true;
  }
  return false;
}

core::Status AppGate::Activate(std::wstring_view route_id) {
  MountedModule* module = FindByRoute(route_id);
  if (module == nullptr) return core::Ok();  // built-in screen: nothing to activate
  if (module->activated) return core::Ok();
  const core::Status bound = module->descriptor->Bind(*module->host);
  if (!bound.ok()) {
    rejects_.push_back({module->manifest.module_id, L"Bind failed: " + bound.Message()});
    return bound;
  }
  module->activated = true;
  return core::Ok();
}

core::Status AppGate::Dispatch(std::wstring_view action, const json::Value& payload,
                               json::Value* state_patch) {
  for (const auto& [registered_action, module_id] : action_map_) {
    if (registered_action != action) continue;
    MountedModule* module = FindByModule(module_id);
    if (module == nullptr) {
      return core::Err(core::ErrorCode::NotFound, L"module not mounted");
    }
    if (!module->activated) {
      const core::Status bound = module->descriptor->Bind(*module->host);
      if (!bound.ok()) return bound;
      module->activated = true;
    }
    return module->descriptor->Handle(action, payload, state_patch);
  }
  return core::Err(core::ErrorCode::NotFound, L"no module declares this action");
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
    peers.push_back({module.manifest.module_id, module.manifest.tab_label,
                     module.manifest.settings_route});
  }
  return peers;
}

}  // namespace modules

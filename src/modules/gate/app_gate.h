// AppGate — the single orchestrator (P3-03, plan Part 3). One choke point:
// nothing else mounts modules, registers actions, or swaps the config.
//
//   Start()          ResolveConfig + CollectModules + PairAndValidate + Mount
//   Activate(route)  lazy Bind() on first visit, never at startup (G1/G2)
//   Dispatch(action) route to the owning module
//
// Degraded mode: a broken module is never mounted, recorded with a reason,
// and everything else keeps working. The window frame, tabs, and routing
// never depend on any module loading.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "modules/contract/module_contract.h"
#include "modules/contract/module_manifest.h"
#include "modules/gate/gate_host.h"
#include "modules/gate/module_validator.h"
#include "modules/gate/settings_store.h"
#include "ui/config/resolved_ui_document.h"

namespace modules {

class ConfigTransactionService;
class GateStartupService;
class SettingsAccessService;

class AppGate final {
 public:
  AppGate();
  ~AppGate();
  AppGate(const AppGate&) = delete;
  AppGate& operator=(const AppGate&) = delete;

  // Runtime path: embedded RCDATA resource (+ optional override file).
  core::Status Start(std::wstring_view override_path = {});
  // Named-resource seam used by the compiled-resource integration test.
  core::Status StartFromResource(std::wstring_view resource_name,
                                 std::wstring_view override_path = {});
  // Test path: explicit texts.
  core::Status StartWithEmbedded(std::wstring_view embedded_text,
                                 std::wstring_view override_text = {});

  // Must be configured before Start. The window procedure routes the supplied
  // completion message to worker::Worker::Settle.
  core::Status ConfigureHostOperations(void* ui_window, unsigned int completion_message,
                                       HostStatePatchHandler state_patch_handler);
  core::Status ConfigureConfigOverridePath(std::wstring path);
  // Test seam; production leaves this empty and uses StateDir/settings.json.
  core::Status ConfigureSettingsStorePath(std::wstring path);

  const ui::config::ResolvedUiDocument* document() const { return document_.get(); }
  bool start_pending() const { return start_pending_; }
  const core::Status& start_status() const { return start_status_; }
  std::uint64_t document_generation() const { return document_generation_; }
  std::vector<PeerInfo> Peers() const;
  const std::vector<RejectEntry>& Rejects() const { return rejects_; }
  const std::vector<CapabilityGrant>& Grants() const { return grants_; }
  const std::vector<SettingsStoreDiagnostic>& SettingsDiagnostics() const;
  DiagnosticsReadModel Diagnostics() const;
  bool Mounted(std::wstring_view module_id) const;

  // Lazy activation: builds the descriptor and Bind()s it on first visit.
  core::Status Activate(std::wstring_view route_id);
  // Route an action to its owning module (auto-activates it).
  core::Status Dispatch(std::wstring_view action, const json::Value& payload,
                        json::Value* state_patch);
  // From ModuleHost::RequestRoute; the gate may refuse unknown routes.
  core::Status RequestRoute(std::wstring_view route_id);
  const std::wstring& current_route() const { return current_route_; }
  // Window lifetime and process lifetime are distinct. Closing the last
  // window releases activated modules; Shutdown is final and idempotent.
  void ReleaseWindowModules();
  void Shutdown();

 private:
  struct MountedModule {
    ModuleManifest manifest;
    std::unique_ptr<ModuleDescriptor> descriptor;
    std::unique_ptr<GateHost> host;
    bool settings_all_granted = false;
    bool config_write_granted = false;
    bool activated = false;
    bool bound_lifetime_started = false;
    bool quarantined = false;
  };

  core::Status PairAndMount(std::wstring_view embedded_text, std::wstring_view override_text);
  MountedModule* FindByRoute(std::wstring_view route_id);
  MountedModule* FindByModule(std::wstring_view module_id);
  core::Status BindModule(MountedModule* module);
  void EnsureHost(MountedModule* module);
  void ReleaseBoundModule(MountedModule* module);
  void Quarantine(MountedModule* module, DiagnosticStage stage,
                  std::wstring reason);
  void ReportModuleStatus(std::wstring_view module_id,
                          const core::Status& status);

  std::unique_ptr<ui::config::ResolvedUiDocument> document_;
  json::Value core_catalog_;
  std::vector<ui::config::ScreenSource> live_sources_;
  std::uint64_t document_generation_ = 0;
  std::unique_ptr<SettingsAccessService> settings_service_;
  std::unique_ptr<ConfigTransactionService> config_service_;
  std::unique_ptr<GateStartupService> startup_service_;
  std::vector<MountedModule> mounted_;
  std::vector<RejectEntry> rejects_;
  std::vector<CapabilityGrant> grants_;
  std::vector<ModuleDiagnosticEntry> runtime_faults_;
  std::vector<ModuleStatusEntry> module_statuses_;
  std::vector<std::pair<std::wstring, std::wstring>> action_map_;  // action -> moduleId
  std::wstring current_route_;
  void* operation_window_ = nullptr;
  unsigned int operation_message_ = 0;
  HostStatePatchHandler state_patch_handler_;
  bool start_pending_ = false;
  core::Status start_status_;
  bool shutdown_ = false;
};

}  // namespace modules

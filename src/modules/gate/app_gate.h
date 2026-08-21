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
#include "ui/config/resolved_ui_document.h"

namespace modules {

struct RejectEntry {
  std::wstring module_id;
  std::wstring reason;
  std::wstring file;
  int line = 0;
  int column = 0;
};

struct CapabilityGrant {
  std::wstring module_id;
  std::wstring capability;
  std::wstring file;
  int line = 0;
  int column = 0;
};

// Reject list of the most recent gate Start — the diagnostics module reads
// this; there is exactly one gate (single choke point).
const std::vector<RejectEntry>& CurrentRejects();

class ConfigTransactionService;
class SettingsAccessService;

class AppGate final {
 public:
  AppGate();
  ~AppGate();
  AppGate(const AppGate&) = delete;
  AppGate& operator=(const AppGate&) = delete;

  // Runtime path: embedded RCDATA resource (+ optional override file).
  core::Status Start(std::wstring_view override_path = {});
  // Test path: explicit texts.
  core::Status StartWithEmbedded(std::wstring_view embedded_text,
                                 std::wstring_view override_text = {});

  // Must be configured before Start. The window procedure routes the supplied
  // completion message to worker::Worker::Settle.
  core::Status ConfigureHostOperations(void* ui_window, unsigned int completion_message,
                                       HostStatePatchHandler state_patch_handler);
  core::Status ConfigureConfigOverridePath(std::wstring path);

  const ui::config::ResolvedUiDocument* document() const { return document_.get(); }
  std::uint64_t document_generation() const { return document_generation_; }
  std::vector<PeerInfo> Peers() const;
  const std::vector<RejectEntry>& Rejects() const { return rejects_; }
  const std::vector<CapabilityGrant>& Grants() const { return grants_; }
  bool Mounted(std::wstring_view module_id) const;

  // Lazy activation: builds the descriptor and Bind()s it on first visit.
  core::Status Activate(std::wstring_view route_id);
  // Route an action to its owning module (auto-activates it).
  core::Status Dispatch(std::wstring_view action, const json::Value& payload,
                        json::Value* state_patch);
  // From ModuleHost::RequestRoute; the gate may refuse unknown routes.
  core::Status RequestRoute(std::wstring_view route_id);
  const std::wstring& current_route() const { return current_route_; }

 private:
  struct MountedModule {
    ModuleManifest manifest;
    std::unique_ptr<ModuleDescriptor> descriptor;
    std::unique_ptr<GateHost> host;
    bool activated = false;
  };

  core::Status PairAndMount(std::wstring_view embedded_text, std::wstring_view override_text);
  MountedModule* FindByRoute(std::wstring_view route_id);
  MountedModule* FindByModule(std::wstring_view module_id);

  std::unique_ptr<ui::config::ResolvedUiDocument> document_;
  std::uint64_t document_generation_ = 0;
  std::unique_ptr<SettingsAccessService> settings_service_;
  std::unique_ptr<ConfigTransactionService> config_service_;
  std::vector<MountedModule> mounted_;
  std::vector<RejectEntry> rejects_;
  std::vector<CapabilityGrant> grants_;
  std::vector<std::pair<std::wstring, std::wstring>> action_map_;  // action -> moduleId
  std::wstring current_route_;
  void* operation_window_ = nullptr;
  unsigned int operation_message_ = 0;
  HostStatePatchHandler state_patch_handler_;
};

}  // namespace modules

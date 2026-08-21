// The parent/child contract (Phase 3, plan Part 3). ADR: docs/adr/0001.
//
// Rules the types enforce by construction:
//   1. A child talks only to its parent, through ModuleHost.
//   2. A child never names a sibling; Peers() returns inert metadata only.
//   3. Settings reach is own-section read/write + global read-only (the host
//      implementation narrows it; the interface cannot widen it).
//   4. The parent knows a child only through ModuleDescriptor.
//   5. Dependencies point one way: child -> contract <- parent.
//   6. Anything beyond default reach is a declared capability, granted by
//      the gate (P3-03), never a backdoor.
//
// APPEND-ONLY RULE: once a module ships against this header, removing or
// re-semanticising a member breaks every module. The synchronous predecessor's
// removal is the one pre-shipping correction recorded by ADR 0001; additions
// only after that contract freeze.
//
// This header is standard library + core/render only; no windows.h.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/json.h"
#include "core/status.h"
#include "render/render_backend.h"

namespace modules {

// What a module sees of the world: its content rect, the current DPI, and a
// way to ask for a repaint. The host owns the surface; the module borrows it.
struct ModuleSurface {
  render::Rect content;
  float dpi = 96.0f;
  // Ask the host to repaint the module's route. Coalesced by the shell.
  void (*invalidate)(void* host_data) = nullptr;
  void* host_data = nullptr;
  void Invalidate() const {
    if (invalidate != nullptr) invalidate(host_data);
  }
};

struct PeerInfo {
  std::wstring module_id;
  std::wstring tab_label;
  std::wstring settings_route;  // empty when the peer ships no settings screen
};

enum class DiagnosticStage {
  Manifest,
  Pairing,
  Capability,
  Bind,
  Dispatch,
  Runtime,
  Settings,
  Startup,
};

struct ModuleDiagnosticEntry {
  std::wstring module_id;
  std::wstring reason;
  std::wstring file;
  int line = 0;
  int column = 0;
  DiagnosticStage stage = DiagnosticStage::Pairing;
};

struct CapabilityGrantInfo {
  std::wstring module_id;
  std::wstring capability;
  std::wstring file;
  int line = 0;
  int column = 0;
};

struct ModuleStatusEntry {
  std::wstring module_id;
  bool ok = true;
  std::wstring message;
};

struct DiagnosticsReadModel {
  std::vector<PeerInfo> accepted;
  std::vector<ModuleDiagnosticEntry> rejected;
  std::vector<CapabilityGrantInfo> grants;
  std::vector<ModuleDiagnosticEntry> runtime_faults;
  // Latest status per module, bounded by mounted module count.
  std::vector<ModuleStatusEntry> statuses;
};

enum class ProcessOperation { Launch, ElevatedLaunch, Capture };

enum class HostOperationKind {
  Launch,
  ElevatedLaunch,
  Capture,
  FolderProbe,
  ConfigSave,
  SettingsLoad,
  SettingsSave,
};

struct AsyncRequestToken {
  std::uint64_t value = 0;

  explicit operator bool() const { return value != 0; }
  friend bool operator==(const AsyncRequestToken&, const AsyncRequestToken&) = default;
};

struct ProcessRequest {
  ProcessOperation operation = ProcessOperation::Launch;
  std::wstring executable;
  std::vector<std::wstring> arguments;
  std::wstring working_directory;
  unsigned long timeout_ms = 10000;
};

struct ProcessOperationResult {
  int exit_code = 0;
  std::wstring output;
  bool timed_out = false;
};

struct FolderProbeRequest {
  std::wstring directory;
  std::vector<std::wstring> relative_files;
};

struct RelativeFilePresence {
  std::wstring relative_path;
  bool present = false;
};

struct FolderProbeResult {
  std::wstring normalized_directory;
  bool directory_exists = false;
  std::vector<RelativeFilePresence> files;
};

struct HostOperationCompletion {
  AsyncRequestToken token;
  std::uint64_t generation = 0;
  HostOperationKind kind = HostOperationKind::Launch;
  core::Status status;
  ProcessOperationResult process;
  FolderProbeResult folder;
};

using HostOperationCallback = std::function<void(const HostOperationCompletion& completion)>;

struct ConfigPreviewToken {
  std::uint64_t value = 0;

  explicit operator bool() const { return value != 0; }
  friend bool operator==(const ConfigPreviewToken&, const ConfigPreviewToken&) = default;
};

struct ConfigDiagnostic {
  std::wstring file;
  int line = 0;
  int column = 0;
  std::wstring message;
};

struct ConfigPreviewResult {
  ConfigPreviewToken token;
  std::vector<std::wstring> affected_routes;
  std::vector<ConfigDiagnostic> diagnostics;
};

class SettingsAllFacet {
 public:
  virtual ~SettingsAllFacet() = default;
  virtual core::Status ReadGlobal(std::wstring_view key, std::wstring* out) = 0;
  virtual core::Status WriteGlobal(std::wstring_view key,
                                   std::wstring_view value) = 0;
  virtual core::Status ReadModule(std::wstring_view module_id,
                                  std::wstring_view key,
                                  std::wstring* out) = 0;
  virtual core::Status WriteModule(std::wstring_view module_id,
                                   std::wstring_view key,
                                   std::wstring_view value) = 0;
  virtual std::vector<PeerInfo> Peers() = 0;
};

class ConfigWriteFacet {
 public:
  virtual ~ConfigWriteFacet() = default;
  virtual core::Status Preview(std::wstring_view candidate,
                               ConfigPreviewResult* result) = 0;
  virtual core::Status Save(ConfigPreviewToken preview,
                            HostOperationCallback callback,
                            AsyncRequestToken* request) = 0;
  virtual core::Status Discard(
      ConfigPreviewToken preview,
      std::vector<std::wstring>* affected_routes) = 0;
};

// Parent hands down. Nothing else is reachable from a child.
class ModuleHost {
 public:
  virtual ~ModuleHost() = default;

  virtual ModuleSurface Surface() = 0;
  // Own settings section read/write; global section read-only. The host
  // narrows by construction — a module cannot address another section.
  virtual core::Status SettingsRead(std::wstring_view key, std::wstring* out) = 0;
  virtual core::Status SettingsReadGlobal(std::wstring_view key, std::wstring* out) = 0;
  virtual core::Status SettingsWrite(std::wstring_view key, std::wstring_view value) = 0;
  // Explicit readiness boundary for the parent-owned settings snapshot.
  // Until this completion arrives, reads return their empty/default value and
  // NotFound. No module infers readiness from a synchronous read result.
  virtual core::Status StartSettingsLoad(HostOperationCallback callback,
                                         AsyncRequestToken* token) = 0;
  // Starts immediately and returns a parent-issued token. The callback is
  // delivered on the UI thread with this host lifetime's generation. Modules
  // pass argv, never a prequoted command line; the parent owns quoting.
  virtual core::Status StartProcess(const ProcessRequest& request,
                                    HostOperationCallback callback,
                                    AsyncRequestToken* token) = 0;
  // Metadata-only directory inspection. relative_files are checked for
  // presence; file contents are never exposed to a module.
  virtual core::Status StartFolderProbe(const FolderProbeRequest& request,
                                        HostOperationCallback callback,
                                        AsyncRequestToken* token) = 0;
  virtual void CancelRequest(AsyncRequestToken token) = 0;
  // Called only from the UI-thread completion path. The parent owns routing
  // the patch to the presenter; a module never reaches frontend types.
  virtual core::Status PublishStatePatch(const json::Value& patch) = 0;
  // Capability-specific facets. A host without the matching gate grant sets
  // the output to null and returns PermissionDenied.
  virtual core::Status GetSettingsAllFacet(SettingsAllFacet** facet) = 0;
  virtual core::Status GetConfigWriteFacet(ConfigWriteFacet** facet) = 0;
  // Immutable parent-owned metadata. The default keeps ordinary modules and
  // test fakes narrow; the diagnostics module receives the real gate model.
  virtual DiagnosticsReadModel Diagnostics() { return {}; }
  // Report success/error; the parent decides how to show it.
  virtual void ReportStatus(const core::Status& status) = 0;
  virtual void Log(std::wstring_view level, std::wstring_view text) = 0;
  // Ask the parent to navigate; the child cannot force it.
  virtual core::Status RequestRoute(std::wstring_view route_id) = 0;
  // Mounted modules as metadata only: no handles, no calls.
  virtual std::vector<PeerInfo> Peers() = 0;
};

// Child hands up. The parent never sees a concrete module type.
class ModuleDescriptor {
 public:
  virtual ~ModuleDescriptor() = default;

  // Must match the moduleId of the module's screen half (pairing rule).
  virtual std::wstring_view ModuleId() const = 0;
  virtual std::wstring_view TabLabel() const = 0;
  virtual int Order() const = 0;
  virtual bool ShowInTabs() const = 0;
  virtual std::wstring_view SettingsRoute() const = 0;  // empty when none
  virtual std::vector<std::wstring> DeclaredActions() const = 0;
  virtual std::vector<std::wstring> DeclaredBindings() const = 0;
  // Must equal the module.json capabilities; the gate refuses a mismatch.
  virtual std::vector<std::wstring> DeclaredCapabilities() const = 0;

  // Called on first activation (lazy), never at startup.
  virtual core::Status Bind(ModuleHost& host) = 0;
  // Route an action this module declared; returns Status (+ optional patch).
  virtual core::Status Handle(std::wstring_view action, const json::Value& payload,
                              json::Value* state_patch) = 0;
  // Called when the last window using the module closes.
  virtual void Release() = 0;
};

}  // namespace modules

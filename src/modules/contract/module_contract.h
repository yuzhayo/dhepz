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
// re-semanticising a member breaks every module. Additions only; a removal
// is a major-version event documented in the ADR.
//
// This header is standard library + core/render only; no windows.h.
#pragma once

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
  // Bulk/blob data scoped to this moduleId only.
  virtual core::Status StorageWrite(std::wstring_view name, std::wstring_view data) = 0;
  virtual core::Status StorageRead(std::wstring_view name, std::wstring* out) = 0;
  // Goes to a worker; never blocks the UI thread (implementation in P3-03+).
  virtual core::Status ProcessRun(std::wstring_view command_line, std::wstring* captured) = 0;
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

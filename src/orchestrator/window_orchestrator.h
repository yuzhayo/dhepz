#pragma once

#include <memory>
#include <string>
#include <vector>

#include "parent/ui/contracts/ui_contract.h"

namespace ui::config {
class ResolvedUiDocument;
}

namespace ui::tabs {
class RouteTabs;
}

namespace modules {
class ModuleStateStore;
struct ModuleDescriptor;
}

namespace orchestrator {

// Owns production window composition. The tray only requests a new app
// window; pairing AppWindow with its core Settings window belongs here.
class WindowOrchestrator final {
 public:
  WindowOrchestrator(void* instance, const ui::config::ResolvedUiDocument* settings_document,
                     const ui::config::ResolvedUiDocument* feature_document,
                     const std::vector<const modules::ModuleDescriptor*>& features);
  ~WindowOrchestrator();

  WindowOrchestrator(const WindowOrchestrator&) = delete;
  WindowOrchestrator& operator=(const WindowOrchestrator&) = delete;

  bool OpenWindow(std::wstring_view route = {});
  void CloseAll();

 private:
  struct WindowSession;

  bool RegisterTabActions(WindowSession* session);
  void BroadcastTabState(WindowSession* source);
  void LoadTabs();
  void PersistTabs();
  void RefreshJumpList();

  void* instance_ = nullptr;
  const ui::config::ResolvedUiDocument* settings_document_ = nullptr;
  const ui::config::ResolvedUiDocument* feature_document_ = nullptr;
  std::vector<const modules::ModuleDescriptor*> features_;
  std::unique_ptr<ui::tabs::RouteTabs> route_tabs_;
  std::wstring route_tabs_load_error_;
  bool route_tabs_warning_shown_ = false;
  bool route_tabs_load_started_ = false;
  bool route_tabs_loaded_ = false;
  bool route_tabs_dirty_ = false;
  WindowSession* route_tabs_loader_ = nullptr;
  std::unique_ptr<modules::ModuleStateStore> state_store_;
  std::vector<std::unique_ptr<WindowSession>> windows_;
};

}  // namespace orchestrator

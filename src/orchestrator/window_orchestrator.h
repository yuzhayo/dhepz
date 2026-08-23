#pragma once

#include <memory>
#include <vector>

namespace ui::config {
class ResolvedUiDocument;
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
                     const modules::ModuleDescriptor* feature);
  ~WindowOrchestrator();

  WindowOrchestrator(const WindowOrchestrator&) = delete;
  WindowOrchestrator& operator=(const WindowOrchestrator&) = delete;

  bool OpenWindow();
  void CloseAll();

 private:
  struct WindowSession;

  void* instance_ = nullptr;
  const ui::config::ResolvedUiDocument* settings_document_ = nullptr;
  const ui::config::ResolvedUiDocument* feature_document_ = nullptr;
  const modules::ModuleDescriptor* feature_ = nullptr;
  std::unique_ptr<modules::ModuleStateStore> state_store_;
  std::vector<std::unique_ptr<WindowSession>> windows_;
};

}  // namespace orchestrator

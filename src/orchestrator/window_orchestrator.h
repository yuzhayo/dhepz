#pragma once

#include <memory>
#include <vector>

namespace ui::config {
class ResolvedUiDocument;
}

namespace orchestrator {

// Owns production window composition. The tray only requests a new app
// window; pairing AppWindow with its core Settings window belongs here.
class WindowOrchestrator final {
 public:
  WindowOrchestrator(void* instance, const ui::config::ResolvedUiDocument* settings_document);
  ~WindowOrchestrator();

  WindowOrchestrator(const WindowOrchestrator&) = delete;
  WindowOrchestrator& operator=(const WindowOrchestrator&) = delete;

  bool OpenWindow();
  void CloseAll();

 private:
  struct WindowSession;

  void* instance_ = nullptr;
  const ui::config::ResolvedUiDocument* settings_document_ = nullptr;
  std::vector<std::unique_ptr<WindowSession>> windows_;
};

}  // namespace orchestrator

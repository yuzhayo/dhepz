#include "orchestrator/window_orchestrator.h"

#include <windows.h>

#include <algorithm>

#include "core/status.h"
#include "ui/app_window/app_window.h"
#include "ui/settings/settings_window.h"

namespace orchestrator {

struct WindowOrchestrator::WindowSession {
  shell::AppWindow window;
  ui::settings::SettingsWindow settings;
};

WindowOrchestrator::WindowOrchestrator(
    void* instance, const ui::config::ResolvedUiDocument* settings_document)
    : instance_(instance), settings_document_(settings_document) {}

WindowOrchestrator::~WindowOrchestrator() { CloseAll(); }

bool WindowOrchestrator::OpenWindow() {
  if (instance_ == nullptr || settings_document_ == nullptr) return false;
  std::erase_if(windows_, [](const std::unique_ptr<WindowSession>& item) {
    return !item->window.alive();
  });

  auto item = std::make_unique<WindowSession>();
  if (!item->window.Create(instance_, 400.0f, 360.0f)) return false;
  WindowSession* const paired = item.get();
  item->window.set_settings_handler([this, paired] {
    const core::Status opened =
        paired->settings.Open(instance_, paired->window.hwnd(), settings_document_);
    if (!opened.ok()) {
      MessageBoxW(static_cast<HWND>(paired->window.hwnd()), opened.Message().c_str(),
                  L"dhepz Settings", MB_OK | MB_ICONERROR);
    }
  });
  item->window.set_close_handler([paired] {
    paired->settings.Close();
    paired->window.Destroy();
  });

  shell::AppWindow* const window = &item->window;
  windows_.push_back(std::move(item));
  window->Show();
  return true;
}

void WindowOrchestrator::CloseAll() { windows_.clear(); }

}  // namespace orchestrator

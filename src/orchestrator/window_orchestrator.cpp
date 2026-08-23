#include "orchestrator/window_orchestrator.h"

#include <windows.h>

#include <algorithm>

#include "core/status.h"
#include "orchestrator/module_host.h"
#include "parent/logic/module_contract.h"
#include "parent/logic/module_state_store.h"
#include "parent/ui/contracts/ui_action_registry.h"
#include "parent/ui/runtime/parent_ui.h"
#include "ui/app_window/app_window.h"
#include "ui/settings/settings_window.h"
#include "platform/paths.h"

namespace orchestrator {

struct WindowOrchestrator::WindowSession {
  shell::AppWindow window;
  ui::settings::SettingsWindow settings;
  ui::containers::ParentUi parent_ui;
  ui::application::UiActionRegistry actions;
  std::unique_ptr<ModuleHostAdapter> host;
  std::unique_ptr<modules::ModuleController> module;

  ~WindowSession() {
    if (host != nullptr) host->Shutdown();
    parent_ui.Detach();
  }
};

WindowOrchestrator::WindowOrchestrator(
    void* instance, const ui::config::ResolvedUiDocument* settings_document,
    const ui::config::ResolvedUiDocument* feature_document,
    const modules::ModuleDescriptor* feature)
    : instance_(instance),
      settings_document_(settings_document),
      feature_document_(feature_document),
      feature_(feature),
      state_store_(std::make_unique<modules::ModuleStateStore>(
          paths::Join(paths::StateDir(), L"settings.json"))) {
  (void)state_store_->Load();
}

WindowOrchestrator::~WindowOrchestrator() { CloseAll(); }

bool WindowOrchestrator::OpenWindow() {
  if (instance_ == nullptr || settings_document_ == nullptr || feature_document_ == nullptr ||
      feature_ == nullptr || feature_->create == nullptr) {
    return false;
  }
  std::erase_if(windows_, [](const std::unique_ptr<WindowSession>& item) {
    return !item->window.alive() && (item->host == nullptr || item->host->Idle());
  });

  auto item = std::make_unique<WindowSession>();
  if (!item->window.Create(instance_, 400.0f, 360.0f)) return false;
  item->host = std::make_unique<ModuleHostAdapter>(&item->window, &item->parent_ui,
                                                   state_store_.get());
  if (!item->host->Start().ok()) return false;
  item->module = feature_->create();
  if (item->module == nullptr) return false;
  item->parent_ui.ApplyPatch(item->module->InitialState(*item->host));
  if (!item->module->Bind(item->host.get(), &item->actions).ok()) return false;
  if (!item->parent_ui.Attach(&item->window, feature_document_, &item->actions).ok()) return false;
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
    if (paired->host != nullptr) paired->host->Deactivate();
    paired->parent_ui.Detach();
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

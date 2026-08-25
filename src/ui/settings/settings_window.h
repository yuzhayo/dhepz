#pragma once

#include <functional>

#include "core/status.h"
#include "parent/ui/config/resolved_ui_document.h"
#include "parent/ui/contracts/ui_action_registry.h"
#include "parent/ui/runtime/parent_ui.h"
#include "ui/app_window/app_window.h"
#include "ui/settings/settings_update_controller.h"

namespace ui::settings {

// Settings is a required core companion to one AppWindow. It is deliberately
// not a feature module: the parent owns its native lifecycle and its JSON
// presenter, while semantic actions remain outside the shell.
class SettingsWindow final {
 public:
  SettingsWindow();
  ~SettingsWindow();

  SettingsWindow(const SettingsWindow&) = delete;
  SettingsWindow& operator=(const SettingsWindow&) = delete;

  core::Status Open(void* instance, void* owner_window,
                    const config::ResolvedUiDocument* document);
  void SetTabLayout(bool multi_row, std::function<void(bool)> changed);
  void ApplyTabLayout(bool multi_row);
  void Close();
  bool alive() const { return window_.alive(); }

 private:
  application::UiActionRegistry actions_;
  shell::AppWindow window_;
  containers::ParentUi parent_ui_;
  SettingsUpdateController updater_;
  bool tabs_multi_row_ = true;
  std::function<void(bool)> tab_layout_changed_;
};

}  // namespace ui::settings

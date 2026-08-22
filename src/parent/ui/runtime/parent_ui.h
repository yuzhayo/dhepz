#pragma once

#include <memory>

#include "core/status.h"
#include "parent/ui/config/resolved_ui_document.h"
#include "parent/ui/contracts/ui_action_registry.h"
#include "parent/ui/contracts/ui_state.h"

namespace shell {
class AppWindow;
}

namespace ui::presenter {
class ScreenPresenter;
}

namespace ui::containers {

class ParentUi final {
 public:
  ParentUi();
  ~ParentUi();

  ParentUi(const ParentUi&) = delete;
  ParentUi& operator=(const ParentUi&) = delete;

  core::Status Attach(shell::AppWindow* window, const config::ResolvedUiDocument* document,
                      application::UiActionRegistry* actions);
  void Detach();

 private:
  shell::AppWindow* window_ = nullptr;
  application::UiState state_;
  std::unique_ptr<presenter::ScreenPresenter> presenter_;
};

}  // namespace ui::containers

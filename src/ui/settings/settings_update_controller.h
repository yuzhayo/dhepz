#pragma once

#include <cstdint>
#include <memory>

#include "core/status.h"
#include "parent/ui/contracts/ui_action_registry.h"
#include "platform/app_update_service.h"

namespace shell {
class AppWindow;
}

namespace ui::containers {
class ParentUi;
}

namespace worker {
class Worker;
}

namespace ui::settings {

class SettingsUpdateController final {
 public:
  SettingsUpdateController();
  ~SettingsUpdateController();

  core::Status Attach(shell::AppWindow* window, containers::ParentUi* parent,
                      application::UiActionRegistry* actions);
  void Detach();
  application::UiPatch InitialPatch() const;

 private:
  application::UiPatch PatchFor(const update::Snapshot& snapshot) const;
  application::UiPatch BeginCheck();
  application::UiPatch BeginInstall();

  update::AppUpdateService service_;
  shell::AppWindow* window_ = nullptr;
  containers::ParentUi* parent_ = nullptr;
  std::unique_ptr<worker::Worker> worker_;
  unsigned int completion_message_ = 0;
  std::uint64_t generation_ = 0;
};

}  // namespace ui::settings

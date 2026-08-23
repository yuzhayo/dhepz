#pragma once

#include <mutex>
#include <string>
#include <string_view>

#include "core/status.h"
#include "parent/ui/contracts/ui_contract.h"

namespace modules {

class ModuleStateStore final {
 public:
  explicit ModuleStateStore(std::wstring path);

  core::Status Load();
  ui::application::UiPatch Restore(std::wstring_view prefix) const;
  core::Status Save(const ui::application::UiPatch& patch);

 private:
  std::wstring path_;
  mutable std::mutex mutex_;
  ui::application::UiPatch state_;
};

}  // namespace modules

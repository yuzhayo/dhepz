#pragma once

#include <cstdint>
#include <memory>

#include "parent/logic/module_contract.h"

namespace shell {
class AppWindow;
}

namespace ui::containers {
class ParentUi;
}

namespace worker {
class Worker;
}

namespace orchestrator {

class ModuleHostAdapter final : public modules::ModuleHost,
                                public modules::BackgroundCapabilities {
 public:
  ModuleHostAdapter(shell::AppWindow* window, ui::containers::ParentUi* parent);
  ~ModuleHostAdapter() override;

  ModuleHostAdapter(const ModuleHostAdapter&) = delete;
  ModuleHostAdapter& operator=(const ModuleHostAdapter&) = delete;

  core::Status Start();
  void Deactivate();
  bool Idle();
  void Shutdown();

  std::wstring DefaultDirectory() const override;
  std::optional<std::wstring> PickFolder(std::wstring_view initial_path) override;
  void RunBackground(modules::BackgroundWork work,
                     modules::BackgroundComplete complete) override;
  void Publish(ui::application::UiPatch patch) override;
  void CloseWindowIfUnpinned() override;

  bool DirectoryExists(std::wstring_view path) const override;
  bool FileExists(std::wstring_view path) const override;
  core::Status StartProcess(const modules::ProcessRequest& request) const override;
  core::Status RunProcess(const modules::ProcessRequest& request,
                          std::wstring* standard_output) const override;

 private:
  shell::AppWindow* window_ = nullptr;
  ui::containers::ParentUi* parent_ = nullptr;
  std::unique_ptr<worker::Worker> worker_;
  unsigned int completion_message_ = 0;
  std::uint64_t generation_ = 0;
};

}  // namespace orchestrator

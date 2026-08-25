#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/status.h"
#include "parent/ui/contracts/ui_action_registry.h"

namespace modules {

struct ProcessRequest {
  std::wstring executable;
  std::vector<std::wstring> arguments;
  std::wstring working_directory;
  bool elevated = false;
  bool hidden = false;
};

class BackgroundCapabilities {
 public:
  virtual ~BackgroundCapabilities() = default;
  virtual bool DirectoryExists(std::wstring_view path) const = 0;
  virtual bool FileExists(std::wstring_view path) const = 0;
  virtual core::Status StartProcess(const ProcessRequest& request) const = 0;
  virtual core::Status RunProcess(const ProcessRequest& request,
                                  std::wstring* standard_output) const = 0;
  virtual core::Status PersistState(const ui::application::UiPatch& patch) const = 0;
};

using BackgroundWork = std::function<core::Status(
    const BackgroundCapabilities& capabilities, const std::atomic<bool>& cancelled)>;
using BackgroundComplete = std::function<void(const core::Status& status)>;

class ModuleHost {
 public:
  virtual ~ModuleHost() = default;
  virtual std::wstring DefaultDirectory() const = 0;
  virtual ui::application::UiPatch RestoredState(std::wstring_view prefix) const = 0;
  virtual std::optional<std::wstring> PickFolder(std::wstring_view initial_path) = 0;
  virtual void RunBackground(BackgroundWork work, BackgroundComplete complete) = 0;
  virtual void Publish(ui::application::UiPatch patch) = 0;
  virtual void CloseWindowIfUnpinned() = 0;
};

class ModuleController {
 public:
  virtual ~ModuleController() = default;
  virtual ui::application::UiPatch InitialState(const ModuleHost& host) const = 0;
  virtual core::Status Bind(ModuleHost* host,
                            ui::application::UiActionRegistry* actions) = 0;
};

struct ModuleDescriptor {
  std::wstring_view id;
  std::wstring_view route_id;
  std::wstring_view screen_name;
  int ui_resource_id = 0;
  std::unique_ptr<ModuleController> (*create)() = nullptr;
  std::wstring_view settings_route_id;
};

}  // namespace modules

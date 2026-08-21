// Terminal feature coordinator. Every process, settings, and filesystem
// operation crosses the narrow parent contract and completes on the UI thread.
#include "modules/contract/module_contract.h"
#include "modules/registry/module_registry.h"
#include "modules/terminal/terminal_logic.h"
#include "modules/terminal/terminal_state.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace terminal {
namespace {

constexpr std::wstring_view kPowerShellVenv = L".venv\\Scripts\\Activate.ps1";
constexpr std::wstring_view kCmdVenv = L".venv\\Scripts\\activate.bat";

json::Value StringArray(const std::vector<std::wstring>& values) {
  json::Value array = json::Value::Array();
  for (const std::wstring& value : values) {
    array.Append(json::Value::String(value));
  }
  return array;
}

json::Value WindowsVenv(std::wstring_view directory,
                        std::wstring_view relative) {
  json::Value venv = json::Value::Object();
  venv.Set(L"kind", json::Value::String(L"windows"));
  venv.Set(L"activate_path",
           json::Value::String(std::wstring(directory) + L"\\" +
                               std::wstring(relative)));
  return venv;
}

bool HasFile(const modules::FolderProbeResult& folder,
             std::wstring_view relative) {
  for (const modules::RelativeFilePresence& file : folder.files) {
    if (file.relative_path == relative) return file.present;
  }
  return false;
}

class TerminalModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"terminal"; }
  std::wstring_view TabLabel() const override { return L"Terminal"; }
  int Order() const override { return 10; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override {
    return {L"terminal:launch", L"terminal:select-folder"};
  }
  std::vector<std::wstring> DeclaredBindings() const override {
    return {L"working_folder", L"recent_folders", L"wsl_distros", L"wsl_distro",
            L"admin", L"powershell_venv", L"cmd_venv", L"venv_available",
            L"venv_enabled", L"busy", L"status", L"launch_enabled"};
  }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }

  core::Status Bind(modules::ModuleHost& host) override {
    host_ = &host;
    ++generation_;
    recents_ = RecentFolders{};
    PublishDefaults();

    const std::uint64_t generation = generation_;
    modules::AsyncRequestToken token;
    const core::Status started = host_->StartSettingsLoad(
        [this, generation](const modules::HostOperationCompletion& completion) {
          if (host_ == nullptr || generation != generation_) return;
          settings_token_ = {};
          if (!completion.status.ok()) {
            host_->ReportStatus(completion.status);
            return;
          }
          recents_.Load(*host_);
          json::Value patch = json::Value::Object();
          patch.Set(L"recent_folders", StringArray(recents_.List()));
          const core::Status published = host_->PublishStatePatch(patch);
          if (!published.ok()) host_->ReportStatus(published);
        },
        &token);
    if (started.ok()) {
      settings_token_ = token;
    } else if (started.Code() != core::ErrorCode::Unsupported) {
      host_->ReportStatus(started);
    }
    return core::Ok();
  }

  core::Status Handle(std::wstring_view action, const json::Value& payload,
                      json::Value* state_patch) override {
    if (host_ == nullptr || state_patch == nullptr) {
      return core::Err(core::ErrorCode::InvalidArgument,
                       L"terminal module is not bound or patch output is missing");
    }
    if (action == L"terminal:select-folder") {
      return SelectFolder(payload, state_patch);
    }
    if (action != L"terminal:launch") {
      return core::Err(core::ErrorCode::NotFound, L"terminal: unknown action");
    }
    return Launch(payload, state_patch);
  }

  void Release() override {
    ++generation_;
    if (host_ != nullptr) {
      if (settings_token_) host_->CancelRequest(settings_token_);
      if (folder_token_) host_->CancelRequest(folder_token_);
      for (modules::AsyncRequestToken token : launch_tokens_) {
        host_->CancelRequest(token);
      }
    }
    settings_token_ = {};
    folder_token_ = {};
    launch_tokens_.clear();
    host_ = nullptr;
  }

 private:
  void PublishDefaults() {
    json::Value patch = json::Value::Object();
    patch.Set(L"working_folder", json::Value::String(L""));
    patch.Set(L"recent_folders", json::Value::Array());
    patch.Set(L"wsl_distros", json::Value::Array());
    patch.Set(L"wsl_distro", json::Value::Null());
    patch.Set(L"admin", json::Value::Bool(false));
    patch.Set(L"powershell_venv", json::Value::Null());
    patch.Set(L"cmd_venv", json::Value::Null());
    patch.Set(L"venv_available", json::Value::Bool(false));
    patch.Set(L"venv_enabled", json::Value::Bool(false));
    patch.Set(L"busy", json::Value::Bool(false));
    patch.Set(L"status", json::Value::String(L"Ready"));
    patch.Set(L"launch_enabled", json::Value::Bool(true));
    const core::Status published = host_->PublishStatePatch(patch);
    if (!published.ok() && published.Code() != core::ErrorCode::Unsupported) {
      host_->ReportStatus(published);
    }
  }

  core::Status SelectFolder(const json::Value& payload,
                            json::Value* state_patch) {
    if (!payload.is_object()) {
      return core::Err(core::ErrorCode::InvalidArgument,
                       L"folder selection payload must be an object");
    }
    const json::Value* folder = payload.Find(L"folder");
    if (folder == nullptr || !folder->is_string() || folder->AsString().empty()) {
      return core::Err(core::ErrorCode::InvalidArgument,
                       L"folder selection requires a non-empty folder");
    }
    const std::uint64_t selection = ++folder_selection_;
    const std::uint64_t generation = generation_;
    modules::FolderProbeRequest request;
    request.directory = folder->AsString();
    request.relative_files = {std::wstring(kPowerShellVenv), std::wstring(kCmdVenv)};

    *state_patch = json::Value::Object();
    state_patch->Set(L"busy", json::Value::Bool(true));
    state_patch->Set(L"launch_enabled", json::Value::Bool(false));
    state_patch->Set(L"status", json::Value::String(L"Validating folder..."));
    modules::AsyncRequestToken token;
    const core::Status started = host_->StartFolderProbe(
        request,
        [this, generation, selection](
            const modules::HostOperationCompletion& completion) {
          if (host_ == nullptr || generation != generation_ ||
              selection != folder_selection_) {
            return;
          }
          folder_token_ = {};
          json::Value patch = json::Value::Object();
          patch.Set(L"busy", json::Value::Bool(false));
          if (!completion.status.ok() || !completion.folder.directory_exists) {
            const core::Status failure = !completion.status.ok()
                ? completion.status
                : core::Err(core::ErrorCode::NotFound,
                            L"folder does not exist or is inaccessible");
            patch.Set(L"launch_enabled", json::Value::Bool(false));
            patch.Set(L"powershell_venv", json::Value::Null());
            patch.Set(L"cmd_venv", json::Value::Null());
            patch.Set(L"venv_available", json::Value::Bool(false));
            patch.Set(L"venv_enabled", json::Value::Bool(false));
            patch.Set(L"status", json::Value::String(failure.Message()));
            host_->ReportStatus(failure);
          } else {
            const bool powershell_available =
                HasFile(completion.folder, kPowerShellVenv);
            const bool cmd_available = HasFile(completion.folder, kCmdVenv);
            patch.Set(L"working_folder",
                      json::Value::String(completion.folder.normalized_directory));
            patch.Set(L"venv_available",
                      json::Value::Bool(powershell_available || cmd_available));
            patch.Set(L"venv_enabled", json::Value::Bool(false));
            patch.Set(L"powershell_venv",
                      powershell_available
                          ? WindowsVenv(completion.folder.normalized_directory,
                                        kPowerShellVenv)
                          : json::Value::Null());
            patch.Set(L"cmd_venv",
                      cmd_available
                          ? WindowsVenv(completion.folder.normalized_directory,
                                        kCmdVenv)
                          : json::Value::Null());
            patch.Set(L"launch_enabled", json::Value::Bool(true));
            patch.Set(L"status", json::Value::String(L"Folder ready"));
            host_->ReportStatus(core::Ok());
          }
          const core::Status published = host_->PublishStatePatch(patch);
          if (!published.ok()) host_->ReportStatus(published);
        },
        &token);
    if (!started.ok()) {
      state_patch->Set(L"busy", json::Value::Bool(false));
      state_patch->Set(L"status", json::Value::String(started.Message()));
      return started;
    }
    folder_token_ = token;
    return core::Ok();
  }

  core::Status Launch(const json::Value& payload, json::Value* state_patch) {
    LaunchSpec spec;
    DHEPZ_RETURN_IF_ERROR(ParseLaunchPayload(payload, &spec));
    modules::ProcessRequest request;
    DHEPZ_RETURN_IF_ERROR(BuildProcessRequest(spec, &request));

    *state_patch = json::Value::Object();
    state_patch->Set(L"busy", json::Value::Bool(true));
    state_patch->Set(L"launch_enabled", json::Value::Bool(false));
    state_patch->Set(L"status", json::Value::String(L"Launching terminal..."));
    modules::AsyncRequestToken token;
    const std::uint64_t generation = generation_;
    const core::Status started = host_->StartProcess(
        request,
        [this, generation, folder = spec.working_dir](
            const modules::HostOperationCompletion& completion) {
          if (host_ == nullptr || generation != generation_) return;
          std::erase(launch_tokens_, completion.token);
          host_->ReportStatus(completion.status);
          json::Value patch = json::Value::Object();
          patch.Set(L"busy", json::Value::Bool(false));
          patch.Set(L"launch_enabled", json::Value::Bool(true));
          patch.Set(L"status", json::Value::String(
              completion.status.ok() ? L"Terminal opened"
                                     : completion.status.Message()));
          if (completion.status.ok() && !folder.empty()) {
            recents_.Add(folder);
            const core::Status saved = recents_.Save(*host_);
            if (!saved.ok()) host_->ReportStatus(saved);
            patch.Set(L"recent_folders", StringArray(recents_.List()));
          }
          const core::Status published = host_->PublishStatePatch(patch);
          if (!published.ok()) host_->ReportStatus(published);
        },
        &token);
    if (!started.ok()) {
      state_patch->Set(L"busy", json::Value::Bool(false));
      state_patch->Set(L"launch_enabled", json::Value::Bool(true));
      state_patch->Set(L"status", json::Value::String(started.Message()));
      return started;
    }
    launch_tokens_.push_back(token);
    return core::Ok();
  }

  modules::ModuleHost* host_ = nullptr;
  RecentFolders recents_;
  std::uint64_t generation_ = 0;
  std::uint64_t folder_selection_ = 0;
  modules::AsyncRequestToken settings_token_;
  modules::AsyncRequestToken folder_token_;
  std::vector<modules::AsyncRequestToken> launch_tokens_;
};

std::unique_ptr<modules::ModuleDescriptor> Make() {
  return std::make_unique<TerminalModule>();
}

const modules::ModuleRegistrar registrar{L"terminal", &Make};

}  // namespace

std::unique_ptr<modules::ModuleDescriptor> MakeTerminalForTests() { return Make(); }

}  // namespace terminal

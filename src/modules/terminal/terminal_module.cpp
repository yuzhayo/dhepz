// Terminal child feature. It owns selection, venv and Windows Terminal policy;
// all OS effects cross the generic parent ModuleHost contract.
#include "modules/contract/module_contract.h"
#include "modules/terminal/terminal_logic.h"
#include "modules/terminal/terminal_state.h"
#include "modules/terminal/venv.h"
#include "modules/terminal/wsl.h"

#include <memory>
#include <utility>
#include <vector>

namespace terminal {
namespace {

json::Value StringArray(const std::vector<std::wstring>& values) {
  json::Value array = json::Value::Array();
  for (const std::wstring& value : values) {
    array.Append(json::Value::String(value));
  }
  return array;
}

class TerminalModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"terminal"; }
  std::wstring_view TabLabel() const override { return L"Terminal"; }
  int Order() const override { return 10; }
  bool ShowInTabs() const override { return false; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override {
    return {L"terminal:launch", L"terminal:browse-folder",
            L"terminal:refresh-wsl"};
  }
  std::vector<std::wstring> DeclaredBindings() const override {
    return {L"working_folder", L"recent_folders", L"wsl_distros",
            L"wsl_distro", L"venv_enabled", L"busy", L"status",
            L"launch_enabled"};
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
          if (!IsCurrent(generation, completion.token, settings_token_) ||
              completion.kind != modules::HostOperationKind::SettingsLoad) {
            return;
          }
          settings_token_ = {};
          if (!completion.status.ok()) {
            host_->ReportStatus(completion.status);
            return;
          }
          recents_.Load(*host_);
          const bool venv_enabled = LoadVenvPreference(*host_);
          json::Value patch = json::Value::Object();
          patch.Set(L"recent_folders", StringArray(recents_.List()));
          patch.Set(L"venv_enabled", json::Value::Bool(venv_enabled));
          if (!recents_.List().empty()) {
            patch.Set(L"working_folder",
                      json::Value::String(recents_.List().front()));
          }
          Publish(std::move(patch));
        },
        &token);
    if (started.ok()) {
      settings_token_ = token;
    } else if (started.Code() != core::ErrorCode::Unsupported) {
      host_->ReportStatus(started);
    }

    const core::Status wsl_started = wsl_.Bind(host);
    if (!wsl_started.ok()) host_->ReportStatus(wsl_started);
    return core::Ok();
  }

  core::Status Handle(std::wstring_view action, const json::Value& payload,
                      json::Value* state_patch) override {
    if (host_ == nullptr || state_patch == nullptr) {
      return core::Err(core::ErrorCode::InvalidArgument,
                       L"terminal module is not bound");
    }
    *state_patch = json::Value::Object();
    if (action == L"terminal:browse-folder") {
      return Browse(payload, state_patch);
    }
    if (action == L"terminal:refresh-wsl") {
      return wsl_.Refresh(state_patch);
    }
    if (action != L"terminal:launch") {
      return core::Err(core::ErrorCode::NotFound, L"terminal: unknown action");
    }
    return BeginLaunch(payload, state_patch);
  }

  void Release() override {
    ++generation_;
    if (host_ != nullptr) {
      if (settings_token_) host_->CancelRequest(settings_token_);
      if (operation_token_) host_->CancelRequest(operation_token_);
    }
    settings_token_ = {};
    operation_token_ = {};
    pending_ = {};
    wsl_.Release();
    host_ = nullptr;
  }

 private:
  bool IsCurrent(std::uint64_t generation, modules::AsyncRequestToken actual,
                 modules::AsyncRequestToken expected) const {
    return host_ != nullptr && generation == generation_ && actual == expected;
  }

  void Publish(json::Value patch) {
    const core::Status published = host_->PublishStatePatch(patch);
    if (!published.ok()) host_->ReportStatus(published);
  }

  void PublishDefaults() {
    json::Value patch = json::Value::Object();
    patch.Set(L"working_folder", json::Value::String(L""));
    patch.Set(L"recent_folders", json::Value::Array());
    wsl_.WriteDefaults(&patch);
    patch.Set(L"venv_enabled", json::Value::Bool(false));
    patch.Set(L"busy", json::Value::Bool(false));
    patch.Set(L"status", json::Value::String(L"Ready"));
    patch.Set(L"launch_enabled", json::Value::Bool(true));
    Publish(std::move(patch));
  }

  core::Status Browse(const json::Value& payload, json::Value* state_patch) {
    modules::FolderPickerRequest request;
    request.title = L"Choose a folder for the terminal";
    if (payload.is_object()) {
      if (const json::Value* initial = payload.Find(L"initial_folder");
          initial != nullptr && initial->is_string()) {
        request.initial_directory = initial->AsString();
      }
    }
    modules::FolderPickerResult selected;
    const core::Status status = host_->PickFolder(request, &selected);
    if (status.Code() == core::ErrorCode::Cancelled) return core::Ok();
    DHEPZ_RETURN_IF_ERROR(status);
    state_patch->Set(L"working_folder",
                     json::Value::String(selected.directory));
    state_patch->Set(L"status", json::Value::String(L"Folder selected"));
    return core::Ok();
  }

  core::Status BeginLaunch(const json::Value& payload,
                           json::Value* state_patch) {
    if (operation_token_) {
      return core::Err(core::ErrorCode::AlreadyExists,
                       L"a terminal operation is already running");
    }
    DHEPZ_RETURN_IF_ERROR(ParseLaunchPayload(payload, &pending_));
    const core::Status saved = SaveVenvPreference(*host_, pending_.venv_enabled);
    if (!saved.ok()) host_->ReportStatus(saved);

    state_patch->Set(L"busy", json::Value::Bool(true));
    state_patch->Set(L"launch_enabled", json::Value::Bool(false));
    state_patch->Set(L"status", json::Value::String(L"Checking folder..."));
    const core::Status started = StartProbe(false);
    if (!started.ok()) {
      host_->ReportStatus(started);
      state_patch->Set(L"busy", json::Value::Bool(false));
      state_patch->Set(L"launch_enabled", json::Value::Bool(true));
      state_patch->Set(L"status", json::Value::String(started.Message()));
    }
    return started;
  }

  core::Status StartProbe(bool after_create) {
    const std::uint64_t generation = generation_;
    modules::AsyncRequestToken token;
    const core::Status started = host_->StartFolderProbe(
        BuildVenvProbe(pending_),
        [this, generation, after_create](
            const modules::HostOperationCompletion& completion) {
          if (!IsCurrent(generation, completion.token, operation_token_) ||
              completion.kind != modules::HostOperationKind::FolderProbe) {
            return;
          }
          operation_token_ = {};
          if (!completion.status.ok()) {
            Finish(completion.status);
          } else if (!completion.folder.directory_exists) {
            Finish(core::Err(core::ErrorCode::NotFound,
                             L"folder does not exist or is inaccessible"));
          } else if (!pending_.venv_enabled ||
                     HasCompatibleVenv(pending_, completion.folder)) {
            const core::Status status = StartTerminal();
            if (!status.ok()) Finish(status);
          } else if (after_create) {
            Finish(core::Err(core::ErrorCode::NotFound,
                             L"virtual environment was not created"));
          } else {
            const core::Status status = StartVenvCreation(false);
            if (!status.ok()) Finish(status);
          }
        },
        &token);
    if (started.ok()) operation_token_ = token;
    return started;
  }

  core::Status StartVenvCreation(bool python_fallback) {
    json::Value patch = json::Value::Object();
    patch.Set(L"status", json::Value::String(L"Creating virtual environment..."));
    Publish(std::move(patch));

    const std::uint64_t generation = generation_;
    modules::AsyncRequestToken token;
    const core::Status started = host_->StartProcess(
        BuildVenvCreateRequest(pending_, python_fallback),
        [this, generation, python_fallback](
            const modules::HostOperationCompletion& completion) {
          if (!IsCurrent(generation, completion.token, operation_token_) ||
              completion.kind != modules::HostOperationKind::Capture) {
            return;
          }
          operation_token_ = {};
          const bool failed = !completion.status.ok() ||
                              completion.process.exit_code != 0;
          if (failed && pending_.target != Target::Wsl && !python_fallback) {
            const core::Status fallback = StartVenvCreation(true);
            if (!fallback.ok()) Finish(fallback);
            return;
          }
          if (failed) {
            const std::wstring message = completion.status.ok()
                ? (completion.process.output.empty()
                       ? L"virtual environment creation failed"
                       : completion.process.output)
                : completion.status.Message();
            Finish(core::Err(core::ErrorCode::IoError, message));
            return;
          }
          const core::Status probe = StartProbe(true);
          if (!probe.ok()) Finish(probe);
        },
        &token);
    if (started.ok()) operation_token_ = token;
    return started;
  }

  core::Status StartTerminal() {
    json::Value patch = json::Value::Object();
    patch.Set(L"status", json::Value::String(L"Opening terminal..."));
    Publish(std::move(patch));

    modules::ProcessRequest request;
    DHEPZ_RETURN_IF_ERROR(BuildProcessRequest(pending_, &request));
    const std::uint64_t generation = generation_;
    modules::AsyncRequestToken token;
    const core::Status started = host_->StartProcess(
        request,
        [this, generation](const modules::HostOperationCompletion& completion) {
          if (!IsCurrent(generation, completion.token, operation_token_) ||
              (completion.kind != modules::HostOperationKind::Launch &&
               completion.kind != modules::HostOperationKind::ElevatedLaunch)) {
            return;
          }
          operation_token_ = {};
          if (completion.status.ok()) {
            recents_.Add(pending_.working_folder);
            const core::Status saved = recents_.Save(*host_);
            if (!saved.ok()) host_->ReportStatus(saved);
          }
          Finish(completion.status);
        },
        &token);
    if (started.ok()) operation_token_ = token;
    return started;
  }

  void Finish(const core::Status& status) {
    operation_token_ = {};
    host_->ReportStatus(status);
    json::Value patch = json::Value::Object();
    patch.Set(L"busy", json::Value::Bool(false));
    patch.Set(L"launch_enabled", json::Value::Bool(true));
    patch.Set(L"status", json::Value::String(
        status.ok() ? L"Terminal opened" : status.Message()));
    if (status.ok()) {
      patch.Set(L"recent_folders", StringArray(recents_.List()));
    }
    Publish(std::move(patch));
  }

  modules::ModuleHost* host_ = nullptr;
  RecentFolders recents_;
  WslSession wsl_;
  LaunchSpec pending_;
  std::uint64_t generation_ = 0;
  modules::AsyncRequestToken settings_token_;
  modules::AsyncRequestToken operation_token_;
};

}  // namespace

std::unique_ptr<modules::ModuleDescriptor> MakeTerminalForTests() {
  return std::make_unique<TerminalModule>();
}

}  // namespace terminal

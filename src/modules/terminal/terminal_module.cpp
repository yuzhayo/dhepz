// Terminal feature coordinator. Process work crosses ModuleHost and completes
// on the UI thread; this child never includes the parent platform layer.
#include "modules/contract/module_contract.h"
#include "modules/registry/module_registry.h"
#include "modules/terminal/terminal_logic.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace terminal {
namespace {

class TerminalModule final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"terminal"; }
  std::wstring_view TabLabel() const override { return L"Terminal"; }
  int Order() const override { return 10; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override {
    return {L"terminal:launch"};
  }
  std::vector<std::wstring> DeclaredBindings() const override {
    return {L"working_folder", L"wsl_distros", L"wsl_distro", L"admin",
            L"powershell_venv", L"cmd_venv", L"venv_available",
            L"venv_enabled", L"busy", L"status", L"launch_enabled"};
  }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }

  core::Status Bind(modules::ModuleHost& host) override {
    host_ = &host;
    ++generation_;
    json::Value defaults = json::Value::Object();
    defaults.Set(L"working_folder", json::Value::String(L""));
    defaults.Set(L"wsl_distros", json::Value::Array());
    defaults.Set(L"wsl_distro", json::Value::Null());
    defaults.Set(L"admin", json::Value::Bool(false));
    defaults.Set(L"powershell_venv", json::Value::Null());
    defaults.Set(L"cmd_venv", json::Value::Null());
    defaults.Set(L"venv_available", json::Value::Bool(false));
    defaults.Set(L"venv_enabled", json::Value::Bool(false));
    defaults.Set(L"busy", json::Value::Bool(false));
    defaults.Set(L"status", json::Value::String(L"Ready"));
    defaults.Set(L"launch_enabled", json::Value::Bool(true));
    const core::Status published = host_->PublishStatePatch(defaults);
    if (!published.ok() && published.Code() != core::ErrorCode::Unsupported) {
      host_->ReportStatus(published);
    }
    return core::Ok();
  }

  core::Status Handle(std::wstring_view action, const json::Value& payload,
                      json::Value* state_patch) override {
    if (action != L"terminal:launch") {
      return core::Err(core::ErrorCode::NotFound, L"terminal: unknown action");
    }
    if (host_ == nullptr || state_patch == nullptr) {
      return core::Err(core::ErrorCode::InvalidArgument,
                       L"terminal module is not bound or patch output is missing");
    }

    LaunchSpec spec;
    DHEPZ_RETURN_IF_ERROR(ParseLaunchPayload(payload, &spec));
    modules::ProcessRequest request;
    DHEPZ_RETURN_IF_ERROR(BuildProcessRequest(spec, &request));

    *state_patch = json::Value::Object();
    state_patch->Set(L"busy", json::Value::Bool(true));
    state_patch->Set(L"launch_enabled", json::Value::Bool(false));
    state_patch->Set(L"status", json::Value::String(L"Launching terminal..."));

    const std::uint64_t generation = generation_;
    modules::AsyncRequestToken token;
    const core::Status started = host_->StartProcess(
        request,
        [this, generation](const modules::HostOperationCompletion& completion) {
          if (host_ == nullptr || generation != generation_) return;
          std::erase(launch_tokens_, completion.token);
          host_->ReportStatus(completion.status);
          json::Value patch = json::Value::Object();
          patch.Set(L"busy", json::Value::Bool(false));
          patch.Set(L"launch_enabled", json::Value::Bool(true));
          patch.Set(L"status", json::Value::String(
              completion.status.ok() ? L"Terminal opened"
                                     : completion.status.Message()));
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

  void Release() override {
    ++generation_;
    if (host_ != nullptr) {
      for (modules::AsyncRequestToken token : launch_tokens_) {
        host_->CancelRequest(token);
      }
    }
    launch_tokens_.clear();
    host_ = nullptr;
  }

 private:
  modules::ModuleHost* host_ = nullptr;
  std::uint64_t generation_ = 0;
  std::vector<modules::AsyncRequestToken> launch_tokens_;
};

std::unique_ptr<modules::ModuleDescriptor> Make() {
  return std::make_unique<TerminalModule>();
}

const modules::ModuleRegistrar registrar{L"terminal", &Make};

}  // namespace

std::unique_ptr<modules::ModuleDescriptor> MakeTerminalForTests() { return Make(); }

}  // namespace terminal

#include <atomic>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/json.h"
#include "framework/test_case.h"
#include "parent/logic/module_registry.h"
#include "parent/ui/config/resolved_ui_document.h"
#include "parent/ui/contracts/ui_state.h"
#include "platform/files.h"
#include "ui/components/input/input_component.h"

namespace {

std::filesystem::path RepositoryRoot() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

std::wstring Read(const std::filesystem::path& path) {
  std::wstring text;
  if (!files::ReadText(path.wstring(), &text).ok()) {
    testing::Fail("could not read P4 UI asset", __FILE__, __LINE__);
  }
  return text;
}

class FakeHost final : public modules::ModuleHost,
                       public modules::BackgroundCapabilities {
 public:
  std::wstring DefaultDirectory() const override { return L"C:\\work"; }

  ui::application::UiPatch RestoredState(std::wstring_view) const override {
    return restored_;
  }

  std::optional<std::wstring> PickFolder(std::wstring_view) override {
    return L"C:\\picked";
  }

  void RunBackground(modules::BackgroundWork work,
                     modules::BackgroundComplete complete) override {
    pending_status_ = work(*this, cancelled_);
    pending_complete_ = std::move(complete);
  }

  void Publish(ui::application::UiPatch patch) override {
    published_.push_back(std::move(patch));
  }

  void CloseWindowIfUnpinned() override { ++close_if_unpinned_requests_; }

  bool DirectoryExists(std::wstring_view) const override { return true; }
  bool FileExists(std::wstring_view) const override { return true; }

  core::Status StartProcess(const modules::ProcessRequest& request) const override {
    started_.push_back(request);
    return start_status_;
  }

  core::Status RunProcess(const modules::ProcessRequest& request,
                          std::wstring* output) const override {
    if (output == nullptr) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"output required");
    }
    output->clear();
    if (request.executable == L"wsl.exe" && request.arguments.size() >= 2 &&
        request.arguments[0] == L"-l") {
      *output = L"Debian\r\n";
    } else if (request.executable == L"wsl.exe" &&
               !request.arguments.empty() && request.arguments.back() == L"C:/work") {
      *output = L"/mnt/c/work\n";
    }
    ran_.push_back(request);
    return core::Ok();
  }

  core::Status PersistState(const ui::application::UiPatch& patch) const override {
    persisted_.push_back(patch);
    return core::Ok();
  }

  void Complete() {
    if (pending_complete_) pending_complete_(pending_status_);
    pending_complete_ = {};
  }

  mutable std::vector<modules::ProcessRequest> started_;
  mutable std::vector<modules::ProcessRequest> ran_;
  std::vector<ui::application::UiPatch> published_;
  ui::application::UiPatch restored_;
  mutable std::vector<ui::application::UiPatch> persisted_;
  core::Status start_status_;
  int close_if_unpinned_requests_ = 0;

 private:
  std::atomic<bool> cancelled_{false};
  core::Status pending_status_;
  modules::BackgroundComplete pending_complete_;
};

}  // namespace

DHEPZ_TEST(P4Terminal, DescriptorJsonAndCoreContractResolveTogether) {
  const modules::ModuleDescriptor* descriptor = modules::ModuleRegistry::Find(L"terminal");
  DHEPZ_CHECK(descriptor != nullptr);
  DHEPZ_CHECK_EQ(descriptor->route_id, std::wstring_view(L"terminal"));
  DHEPZ_CHECK_EQ(descriptor->settings_route_id,
                 std::wstring_view(L"terminal.settings"));

  json::Value core_catalog;
  DHEPZ_CHECK(json::Parse(Read(RepositoryRoot() / L"assets" / L"ui" / L"core.json"),
                          &core_catalog)
                  .ok());
  std::vector<ui::config::Diagnostic> diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> document;
  DHEPZ_CHECK(ui::config::ResolveDocument(
                  core_catalog,
                  {{std::wstring(descriptor->screen_name),
                    Read(RepositoryRoot() / L"src" / L"modules" / L"terminal" /
                         L"terminal.json")}},
                  &diagnostics, &document)
                  .ok());
  DHEPZ_CHECK(document != nullptr);
  DHEPZ_CHECK(document->FindRoute(L"terminal") != nullptr);
  const ui::config::Route* settings = document->FindRoute(L"terminal.settings");
  DHEPZ_CHECK(settings != nullptr);
  DHEPZ_CHECK_FALSE(settings->show_in_tabs);
  ui::config::Rgba accent;
  DHEPZ_CHECK(document->Token(L"dark", L"accent", &accent));
}

DHEPZ_TEST(P4Terminal, ActionsUseTheSameExplicitWindowsTerminalProfilesAsV1) {
  const modules::ModuleDescriptor* descriptor = modules::ModuleRegistry::Find(L"terminal");
  DHEPZ_CHECK(descriptor != nullptr);
  std::unique_ptr<modules::ModuleController> controller = descriptor->create();
  DHEPZ_CHECK(controller != nullptr);

  FakeHost host;
  ui::application::UiActionRegistry actions;
  DHEPZ_CHECK(controller->Bind(&host, &actions).ok());
  ui::application::UiState state;
  DHEPZ_CHECK(state.Apply(controller->InitialState(host)));

  ui::application::UiEvent browse{L"terminal.browse", L"browse-folder", {}};
  DHEPZ_CHECK(state.Apply(actions.Dispatch(browse, state)));
  DHEPZ_CHECK_EQ(state.Text(L"terminal.path"), std::wstring(L"C:\\picked"));

  state.Set(L"terminal.path", std::wstring(L"C:\\work"));
  state.Set(L"terminal.venv", true);
  ui::application::UiEvent wsl{L"terminal.launch.wsl", L"ubuntu-wsl", {}};
  DHEPZ_CHECK(state.Apply(actions.Dispatch(wsl, state)));
  host.Complete();
  DHEPZ_CHECK_FALSE(host.started_.empty());
  const modules::ProcessRequest& request = host.started_.back();
  DHEPZ_CHECK_EQ(request.executable, std::wstring(L"wt.exe"));
  DHEPZ_CHECK_FALSE(request.elevated);
  DHEPZ_CHECK(std::find(request.arguments.begin(), request.arguments.end(), L"Ubuntu") !=
              request.arguments.end());
  DHEPZ_CHECK(std::find(request.arguments.begin(), request.arguments.end(), L"-p") !=
              request.arguments.end());
  DHEPZ_CHECK(std::find_if(request.arguments.begin(), request.arguments.end(),
                           [](const auto& argument) {
                             return argument.find(L".venv-wsl/bin/activate") !=
                                    std::wstring::npos;
                           }) != request.arguments.end());
  DHEPZ_CHECK(std::find_if(host.ran_.begin(), host.ran_.end(), [](const auto& process) {
                return std::any_of(process.arguments.begin(), process.arguments.end(),
                                   [](const auto& argument) {
                                     return argument.find(L".venv-wsl") !=
                                            std::wstring::npos;
                                   });
              }) != host.ran_.end());

  ui::application::UiEvent normal{L"terminal.launch.default", L"windows-terminal", {}};
  const ui::application::UiPatch normal_patch = actions.Dispatch(normal, state);
  DHEPZ_CHECK_FALSE(normal_patch.empty());
  (void)state.Apply(normal_patch);
  host.Complete();
  DHEPZ_CHECK_FALSE(host.started_.back().elevated);
  DHEPZ_CHECK(std::find(host.started_.back().arguments.begin(),
                        host.started_.back().arguments.end(), L"PowerShell") !=
              host.started_.back().arguments.end());
  DHEPZ_CHECK(std::find(host.started_.back().arguments.begin(),
                        host.started_.back().arguments.end(), L"pwsh.exe") !=
              host.started_.back().arguments.end());
  DHEPZ_CHECK(std::find_if(host.started_.back().arguments.begin(),
                           host.started_.back().arguments.end(), [](const auto& argument) {
                             return argument.find(L"Activate.ps1") != std::wstring::npos;
                           }) != host.started_.back().arguments.end());

  ui::application::UiEvent admin{L"terminal.launch.admin", L"windows-terminal-admin", {}};
  const ui::application::UiPatch admin_patch = actions.Dispatch(admin, state);
  DHEPZ_CHECK_FALSE(admin_patch.empty());
  (void)state.Apply(admin_patch);
  host.Complete();
  DHEPZ_CHECK(host.started_.back().elevated);
  DHEPZ_CHECK(std::find(host.started_.back().arguments.begin(),
                        host.started_.back().arguments.end(), L"PowerShell") !=
              host.started_.back().arguments.end());
}

DHEPZ_TEST(P4Terminal, SuccessfulLaunchClosesOnlyThroughTheParentRequest) {
  const modules::ModuleDescriptor* descriptor = modules::ModuleRegistry::Find(L"terminal");
  DHEPZ_CHECK(descriptor != nullptr);
  std::unique_ptr<modules::ModuleController> controller = descriptor->create();
  DHEPZ_CHECK(controller != nullptr);

  FakeHost host;
  ui::application::UiActionRegistry actions;
  DHEPZ_CHECK(controller->Bind(&host, &actions).ok());
  ui::application::UiState state;
  DHEPZ_CHECK(state.Apply(controller->InitialState(host)));

  ui::application::UiEvent launch{L"terminal.launch.default", L"windows-terminal", {}};
  DHEPZ_CHECK(state.Apply(actions.Dispatch(launch, state)));
  host.Complete();
  DHEPZ_CHECK_EQ(host.close_if_unpinned_requests_, 1);

  std::unique_ptr<modules::ModuleController> failed_controller = descriptor->create();
  DHEPZ_CHECK(failed_controller != nullptr);
  FakeHost failed_host;
  failed_host.start_status_ =
      DHEPZ_ERR(core::ErrorCode::Internal, L"Windows Terminal could not be opened");
  ui::application::UiActionRegistry failed_actions;
  DHEPZ_CHECK(failed_controller->Bind(&failed_host, &failed_actions).ok());
  ui::application::UiState failed_state;
  DHEPZ_CHECK(failed_state.Apply(failed_controller->InitialState(failed_host)));
  DHEPZ_CHECK(failed_state.Apply(failed_actions.Dispatch(launch, failed_state)));
  failed_host.Complete();
  DHEPZ_CHECK_EQ(failed_host.close_if_unpinned_requests_, 0);
}

DHEPZ_TEST(P4Terminal, RestoresAndPersistsPathHistoryForTheInputDropdown) {
  const modules::ModuleDescriptor* descriptor = modules::ModuleRegistry::Find(L"terminal");
  DHEPZ_CHECK(descriptor != nullptr);
  std::unique_ptr<modules::ModuleController> controller = descriptor->create();
  DHEPZ_CHECK(controller != nullptr);

  FakeHost host;
  host.restored_.changes.push_back({L"terminal.path", std::wstring(L"C:\\saved")});
  host.restored_.changes.push_back(
      {L"terminal.recent_paths",
       std::vector<std::wstring>{L"C:\\saved", L"D:\\previous"}});
  ui::application::UiState state;
  DHEPZ_CHECK(state.Apply(controller->InitialState(host)));
  DHEPZ_CHECK_EQ(state.Text(L"terminal.path"), std::wstring(L"C:\\saved"));
  const std::vector<std::wstring>* restored = state.Strings(L"terminal.recent_paths");
  DHEPZ_CHECK(restored != nullptr);
  DHEPZ_CHECK_EQ(restored->size(), static_cast<std::size_t>(2));

  ui::config::ComponentNode input(L"input", L"terminal-path");
  input.SetProperty(L"suggestions_binding",
                    json::Value::String(L"terminal.recent_paths"));
  const ui::components::ComponentDescriptor descriptor_component =
      ui::components::CreateInputComponent();
  DHEPZ_CHECK(descriptor_component.has_overlay != nullptr);
  DHEPZ_CHECK(descriptor_component.has_overlay(input, state));

  ui::application::UiActionRegistry actions;
  DHEPZ_CHECK(controller->Bind(&host, &actions).ok());
  state.Set(L"terminal.path", std::wstring(L"E:\\next"));
  ui::application::UiEvent launch{L"terminal.launch.default", L"windows-terminal", {}};
  DHEPZ_CHECK(state.Apply(actions.Dispatch(launch, state)));
  host.Complete();
  DHEPZ_CHECK_EQ(host.persisted_.size(), static_cast<std::size_t>(1));
  ui::application::UiState persisted;
  DHEPZ_CHECK(persisted.Apply(host.persisted_.front()));
  DHEPZ_CHECK_EQ(persisted.Text(L"terminal.path"), std::wstring(L"E:\\next"));
  const std::vector<std::wstring>* history = persisted.Strings(L"terminal.recent_paths");
  DHEPZ_CHECK(history != nullptr);
  DHEPZ_CHECK_EQ(history->size(), static_cast<std::size_t>(3));
  DHEPZ_CHECK_EQ(history->front(), std::wstring(L"E:\\next"));
}

#include "parent/logic/module_registry.h"

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "modules/terminal/terminal_resources.h"

namespace modules::terminal {
namespace {

enum class LaunchKind { Default, Admin, Wsl };

ui::application::UiPatch Patch(std::wstring path, ui::application::UiValue value) {
  ui::application::UiPatch patch;
  patch.changes.push_back({std::move(path), std::move(value)});
  return patch;
}

void Add(ui::application::UiPatch* patch, std::wstring path,
         ui::application::UiValue value) {
  patch->changes.push_back({std::move(path), std::move(value)});
}

bool EqualsInsensitive(std::wstring_view left, std::wstring_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(), right.end(),
                    [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
}

std::wstring Trim(std::wstring_view text) {
  const std::size_t first = text.find_first_not_of(L" \t\r\n");
  if (first == std::wstring_view::npos) return {};
  const std::size_t last = text.find_last_not_of(L" \t\r\n");
  return std::wstring(text.substr(first, last - first + 1));
}

std::vector<std::wstring> RecentPaths(const ui::application::UiState& state,
                                      std::wstring_view selected) {
  std::vector<std::wstring> recent;
  const std::wstring current = Trim(selected);
  if (!current.empty()) recent.push_back(current);
  const std::vector<std::wstring>* existing = state.Strings(L"terminal.recent_paths");
  if (existing != nullptr) {
    for (const std::wstring& path : *existing) {
      if (path.empty() || std::any_of(recent.begin(), recent.end(), [&path](const auto& item) {
            return EqualsInsensitive(item, path);
          })) {
        continue;
      }
      recent.push_back(path);
      if (recent.size() == 8) break;
    }
  }
  return recent;
}

std::wstring UserMessage(const core::Status& status) {
  if (status.ok()) return {};
  std::wstring message = status.Message();
  const std::size_t context = message.find(L" [");
  if (context != std::wstring::npos) message.resize(context);
  return message;
}

std::vector<std::wstring> NonEmptyLines(std::wstring text) {
  text.erase(std::remove(text.begin(), text.end(), L'\0'), text.end());
  std::vector<std::wstring> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find(L'\n', start);
    std::wstring line = Trim(text.substr(start, end == std::wstring::npos ? end : end - start));
    if (!line.empty()) lines.push_back(std::move(line));
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  return lines;
}

struct WslTarget {
  std::wstring distribution;
  std::wstring linux_path;
};

bool ParseWslUnc(std::wstring_view input, WslTarget* target) {
  if (target == nullptr) return false;
  constexpr std::wstring_view prefixes[]{L"\\\\wsl.localhost\\", L"\\\\wsl$\\"};
  for (std::wstring_view prefix : prefixes) {
    if (input.size() <= prefix.size()) continue;
    bool match = true;
    for (std::size_t index = 0; index < prefix.size(); ++index) {
      if (std::towlower(input[index]) != std::towlower(prefix[index])) {
        match = false;
        break;
      }
    }
    if (!match) continue;
    const std::size_t separator = input.find(L'\\', prefix.size());
    target->distribution = std::wstring(input.substr(
        prefix.size(), separator == std::wstring_view::npos ? input.size() - prefix.size()
                                                                 : separator - prefix.size()));
    target->linux_path = L"/";
    if (separator != std::wstring_view::npos) {
      target->linux_path.append(input.substr(separator + 1));
      std::replace(target->linux_path.begin(), target->linux_path.end(), L'\\', L'/');
    }
    return !target->distribution.empty();
  }
  return false;
}

core::Status ResolveWslTarget(const BackgroundCapabilities& capabilities,
                              std::wstring_view path, WslTarget* target) {
  if (target == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"WSL target output is required");
  }
  if (ParseWslUnc(path, target)) return core::Ok();

  ProcessRequest list;
  list.executable = L"wsl.exe";
  list.arguments = {L"-l", L"-q"};
  list.hidden = true;
  std::wstring output;
  DHEPZ_RETURN_IF_ERROR(capabilities.RunProcess(list, &output));
  const std::vector<std::wstring> distributions = NonEmptyLines(std::move(output));
  if (distributions.empty()) {
    return DHEPZ_ERR(core::ErrorCode::NotFound, L"No WSL distribution is installed");
  }
  target->distribution = distributions.front();
  const auto ubuntu = std::find_if(distributions.begin(), distributions.end(),
                                   [](const std::wstring& item) {
                                     return EqualsInsensitive(item, L"Ubuntu") ||
                                            item.rfind(L"Ubuntu-", 0) == 0;
                                   });
  if (ubuntu != distributions.end()) target->distribution = *ubuntu;

  ProcessRequest convert;
  convert.executable = L"wsl.exe";
  std::wstring portable_path(path);
  std::replace(portable_path.begin(), portable_path.end(), L'\\', L'/');
  convert.arguments = {L"-d", target->distribution, L"--", L"wslpath", L"-a", L"-u",
                       std::move(portable_path)};
  convert.hidden = true;
  DHEPZ_RETURN_IF_ERROR(capabilities.RunProcess(convert, &output));
  target->linux_path = Trim(output);
  if (target->linux_path.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"WSL could not resolve the folder path");
  }
  return core::Ok();
}

core::Status EnsureWindowsVenv(const BackgroundCapabilities& capabilities,
                               std::wstring_view path) {
  const std::wstring root(path);
  const std::wstring python = root + L"\\.venv\\Scripts\\python.exe";
  if (capabilities.FileExists(python)) return core::Ok();
  ProcessRequest create;
  create.executable = L"py.exe";
  create.arguments = {L"-3", L"-m", L"venv", root + L"\\.venv"};
  create.working_directory = root;
  create.hidden = true;
  std::wstring ignored;
  core::Status status = capabilities.RunProcess(create, &ignored);
  if (status.ok()) return status;
  create.executable = L"python.exe";
  create.arguments = {L"-m", L"venv", root + L"\\.venv"};
  ignored.clear();
  return capabilities.RunProcess(create, &ignored);
}

core::Status EnsureWslVenv(const BackgroundCapabilities& capabilities,
                           const WslTarget& target, std::wstring_view environment) {
  ProcessRequest create;
  create.executable = L"wsl.exe";
  create.arguments = {L"-d", target.distribution, L"--cd", target.linux_path, L"--", L"sh",
                      L"-lc", L"test -x " + std::wstring(environment) +
                                    L"/bin/python || python3 -m venv --prompt .venv " +
                                    std::wstring(environment)};
  create.hidden = true;
  std::wstring ignored;
  return capabilities.RunProcess(create, &ignored);
}

core::Status Launch(const BackgroundCapabilities& capabilities, LaunchKind kind,
                     std::wstring path, bool prepare_venv,
                     const std::atomic<bool>& cancelled,
                     const std::atomic<bool>* request_cancelled) {
  const auto is_cancelled = [&] {
    return cancelled.load() ||
           (request_cancelled != nullptr && request_cancelled->load());
  };
  path = Trim(path);
  if (path.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"Choose a terminal folder first");
  }
  if (!capabilities.DirectoryExists(path)) {
    return DHEPZ_ERR(core::ErrorCode::NotFound, L"The selected folder does not exist");
  }
  if (is_cancelled()) return DHEPZ_ERR(core::ErrorCode::Cancelled, L"Launch cancelled");

  if (kind == LaunchKind::Wsl) {
    ProcessRequest request;
    request.executable = L"wt.exe";
    request.working_directory = path;
    if (!prepare_venv) {
      request.arguments = {L"-w", L"new", L"new-tab", L"-p", L"Ubuntu", L"-d", path};
      return capabilities.StartProcess(request);
    }

    WslTarget wsl;
    DHEPZ_RETURN_IF_ERROR(ResolveWslTarget(capabilities, path, &wsl));
    if (is_cancelled()) return DHEPZ_ERR(core::ErrorCode::Cancelled, L"Launch cancelled");
    std::wstring environment = L".venv";
    if (capabilities.FileExists(path + L"\\.venv\\Scripts\\python.exe")) {
      environment = L".venv-wsl";
    }
    DHEPZ_RETURN_IF_ERROR(EnsureWslVenv(capabilities, wsl, environment));
    if (is_cancelled()) return DHEPZ_ERR(core::ErrorCode::Cancelled, L"Launch cancelled");
    request.arguments = {L"-w", L"new", L"new-tab", L"-p", L"Ubuntu", L"wsl.exe",
                         L"-d", wsl.distribution, L"--cd", wsl.linux_path};
    const std::wstring startup =
        L"exec bash --rcfile <(printf '%s\\n' 'source ~/.bashrc' 'source " +
        environment + L"/bin/activate') -i";
    request.arguments.insert(request.arguments.end(), {L"--", L"bash", L"-lc", startup});
    return capabilities.StartProcess(request);
  }

  if (prepare_venv) DHEPZ_RETURN_IF_ERROR(EnsureWindowsVenv(capabilities, path));
  if (is_cancelled()) return DHEPZ_ERR(core::ErrorCode::Cancelled, L"Launch cancelled");
  ProcessRequest request;
  request.executable = L"wt.exe";
  request.arguments = {L"-w", L"new", L"new-tab", L"-p", L"PowerShell", L"-d", path};
  if (prepare_venv) {
    request.arguments.insert(request.arguments.end(),
                             {L"pwsh.exe", L"-NoExit", L"-ExecutionPolicy", L"Bypass",
                              L"-Command", L"& '.\\.venv\\Scripts\\Activate.ps1'"});
  }
  request.working_directory = path;
  request.elevated = kind == LaunchKind::Admin;
  return capabilities.StartProcess(request);
}

class TerminalController final : public ModuleController {
 public:
  ui::application::UiPatch InitialState(const ModuleHost& host) const override {
    const std::wstring default_directory = host.DefaultDirectory();
    ui::application::UiState state;
    ui::application::UiPatch patch;
    Add(&patch, L"terminal.path", default_directory);
    Add(&patch, L"terminal.recent_paths",
        default_directory.empty() ? std::vector<std::wstring>{}
                                  : std::vector<std::wstring>{default_directory});
    Add(&patch, L"terminal.venv", false);
    Add(&patch, L"terminal.status", std::wstring{});
    Add(&patch, L"terminal.busy", false);
    (void)state.Apply(patch);
    (void)state.Apply(host.RestoredState(L"terminal."));
    const std::wstring restored_path = state.Text(L"terminal.path", default_directory);
    Add(&patch, L"terminal.path", restored_path);
    Add(&patch, L"terminal.recent_paths", RecentPaths(state, restored_path));
    return patch;
  }

  core::Status Bind(ModuleHost* host, ui::application::UiActionRegistry* actions) override {
    if (host == nullptr || actions == nullptr) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"Terminal module requires its parent contract");
    }
    host_ = host;
    if (!actions->Register(L"terminal.browse", [this](const auto&, const auto& state) {
          const auto selected = host_->PickFolder(state.Text(L"terminal.path"));
          if (!selected.has_value()) return ui::application::UiPatch{};
          ui::application::UiPatch patch = Patch(L"terminal.path", *selected);
           Add(&patch, L"terminal.recent_paths", RecentPaths(state, *selected));
           return patch;
         }) ||
        !actions->Register(L"terminal.venv.changed", [this](const auto&, const auto& state) {
          if (state.Bool(L"terminal.venv")) return ui::application::UiPatch{};
          CancelPendingVenv();
          ui::application::UiPatch patch;
          Add(&patch, L"terminal.busy", false);
          Add(&patch, L"terminal.status", std::wstring{});
          return patch;
        }) ||
        !RegisterLaunch(actions, L"terminal.launch.admin", LaunchKind::Admin) ||
        !RegisterLaunch(actions, L"terminal.launch.default", LaunchKind::Default) ||
        !RegisterLaunch(actions, L"terminal.launch.wsl", LaunchKind::Wsl)) {
      return DHEPZ_ERR(core::ErrorCode::AlreadyExists, L"Terminal action registration failed");
    }
    return core::Ok();
  }

 private:
  bool RegisterLaunch(ui::application::UiActionRegistry* actions, std::wstring action,
                       LaunchKind kind) {
    return actions->Register(std::move(action), [this, kind](const auto&, const auto& state) {
      const std::wstring path = state.Text(L"terminal.path");
      const bool prepare_venv = state.Bool(L"terminal.venv");
      const std::vector<std::wstring> recent_paths = RecentPaths(state, path);
      CancelPendingVenv();
      std::shared_ptr<std::atomic<bool>> request_cancelled;
      if (prepare_venv) {
        request_cancelled = std::make_shared<std::atomic<bool>>(false);
        pending_venv_ = request_cancelled;
      }
      host_->RunBackground(
          [kind, path, prepare_venv, recent_paths, request_cancelled](
              const BackgroundCapabilities& capabilities,
              const std::atomic<bool>& cancelled) {
            DHEPZ_RETURN_IF_ERROR(Launch(capabilities, kind, path, prepare_venv,
                                         cancelled, request_cancelled.get()));
            ui::application::UiPatch persisted;
            Add(&persisted, L"terminal.path", path);
            Add(&persisted, L"terminal.recent_paths", recent_paths);
            return capabilities.PersistState(persisted);
          },
          [this, request_cancelled](const core::Status& status) {
            if (request_cancelled != nullptr && request_cancelled->load()) return;
            if (pending_venv_ == request_cancelled) pending_venv_.reset();
            ui::application::UiPatch complete;
            Add(&complete, L"terminal.busy", false);
            Add(&complete, L"terminal.status", UserMessage(status));
            host_->Publish(std::move(complete));
            if (status.ok()) host_->CloseWindowIfUnpinned();
          });
      ui::application::UiPatch pending;
      Add(&pending, L"terminal.busy", true);
      Add(&pending, L"terminal.recent_paths", recent_paths);
      Add(&pending, L"terminal.status",
          prepare_venv ? std::wstring(L"Checking Python venv...")
                       : std::wstring(L"Opening terminal..."));
      return pending;
    });
  }

  void CancelPendingVenv() {
    if (pending_venv_ != nullptr) pending_venv_->store(true);
    pending_venv_.reset();
  }

  ModuleHost* host_ = nullptr;
  std::shared_ptr<std::atomic<bool>> pending_venv_;
};

std::unique_ptr<ModuleController> CreateTerminalController() {
  return std::make_unique<TerminalController>();
}

const ModuleDescriptor kTerminalDescriptor{
    L"terminal", L"terminal", L"modules/terminal/terminal.json", IDR_TERMINAL_SCREEN_JSON,
    &CreateTerminalController};
const ModuleRegistrar kTerminalRegistration(&kTerminalDescriptor);

}  // namespace
}  // namespace modules::terminal

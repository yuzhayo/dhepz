#include "modules/terminal/terminal_logic.h"

#include <cwctype>
#include <set>

namespace terminal {
namespace {

bool EndsWithInsensitive(std::wstring_view value, std::wstring_view suffix) {
  if (suffix.size() > value.size()) return false;
  const std::size_t offset = value.size() - suffix.size();
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    if (std::towlower(value[offset + i]) != std::towlower(suffix[i])) return false;
  }
  return true;
}

bool IsWindowsPath(std::wstring_view path) {
  return path.empty() ||
         (path.size() >= 3 && std::iswalpha(path[0]) && path[1] == L':' &&
          (path[2] == L'\\' || path[2] == L'/')) ||
         path.rfind(L"\\\\", 0) == 0;
}

bool IsLinuxPath(std::wstring_view path) {
  return !path.empty() && path.front() == L'/';
}

core::Status OptionalString(const json::Value& payload, std::wstring_view key,
                            std::wstring* out) {
  const json::Value* value = payload.Find(key);
  if (value == nullptr || value->is_null()) {
    out->clear();
    return core::Ok();
  }
  if (!value->is_string()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"terminal payload '" + std::wstring(key) +
                         L"' must be a string or null");
  }
  *out = value->AsString();
  return core::Ok();
}

}  // namespace

core::Status ParseLaunchPayload(const json::Value& payload, LaunchSpec* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"terminal launch output is required");
  }
  *out = {};
  if (!payload.is_object()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"terminal launch payload must be an object");
  }
  static const std::set<std::wstring> allowed = {
      L"shell", L"wsl_distro", L"admin", L"working_folder", L"venv_enabled",
      L"venv"};
  for (const auto& [key, value] : payload.members()) {
    (void)value;
    if (!allowed.contains(key)) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"unknown terminal payload field '" + key + L"'");
    }
  }

  const json::Value* shell = payload.Find(L"shell");
  if (shell == nullptr || !shell->is_string()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"terminal payload requires string 'shell'");
  }
  if (shell->AsString() == L"powershell") out->shell = Shell::PowerShell;
  else if (shell->AsString() == L"cmd") out->shell = Shell::Cmd;
  else if (shell->AsString() == L"wsl") out->shell = Shell::Wsl;
  else {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"terminal shell must be powershell, cmd, or wsl");
  }

  DHEPZ_RETURN_IF_ERROR(OptionalString(payload, L"wsl_distro", &out->wsl_distro));
  DHEPZ_RETURN_IF_ERROR(OptionalString(payload, L"working_folder", &out->working_dir));
  if (const json::Value* admin = payload.Find(L"admin");
      admin != nullptr && !admin->is_null()) {
    if (!admin->is_bool()) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"terminal payload 'admin' must be a bool or null");
    }
    out->admin = admin->AsBool();
  }

  bool venv_enabled = true;
  if (const json::Value* enabled = payload.Find(L"venv_enabled");
      enabled != nullptr && !enabled->is_null()) {
    if (!enabled->is_bool()) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"terminal payload 'venv_enabled' must be a bool or null");
    }
    venv_enabled = enabled->AsBool();
  }

  if (const json::Value* venv = payload.Find(L"venv");
      venv != nullptr && !venv->is_null()) {
    if (!venv->is_object()) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"terminal payload 'venv' must be an object or null");
    }
    for (const auto& [key, value] : venv->members()) {
      (void)value;
      if (key != L"kind" && key != L"activate_path") {
        return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                         L"unknown terminal venv field '" + key + L"'");
      }
    }
    const std::wstring kind = venv->StringField(L"kind");
    const json::Value* path = venv->Find(L"activate_path");
    if ((kind != L"windows" && kind != L"linux") || path == nullptr ||
        !path->is_string() || path->AsString().empty()) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"venv requires kind windows/linux and activate_path");
    }
    out->venv.kind = kind == L"windows" ? PathKind::Windows : PathKind::Linux;
    out->venv.activate_path = path->AsString();
  }
  if (!venv_enabled) out->venv = {};
  return core::Ok();
}

core::Status BuildProcessRequest(const LaunchSpec& spec,
                                 modules::ProcessRequest* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"process request output is required");
  }
  *out = {};
  out->operation = spec.admin ? modules::ProcessOperation::ElevatedLaunch
                              : modules::ProcessOperation::Launch;

  if (spec.shell == Shell::Wsl) {
    if (spec.admin) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"elevated launch is not supported for WSL");
    }
    if (spec.wsl_distro.empty()) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"WSL launch requires a distro");
    }
    if (!spec.working_dir.empty() && !IsLinuxPath(spec.working_dir)) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"WSL working folder must be an absolute Linux path");
    }
    if (spec.venv.kind != PathKind::None &&
        (spec.venv.kind != PathKind::Linux ||
         !IsLinuxPath(spec.venv.activate_path) ||
         !EndsWithInsensitive(spec.venv.activate_path, L"/bin/activate"))) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"WSL venv must use an absolute Linux bin/activate path");
    }
    out->executable = L"wsl.exe";
    out->arguments = {L"-d", spec.wsl_distro};
    if (!spec.working_dir.empty()) {
      out->arguments.push_back(L"--cd");
      out->arguments.push_back(spec.working_dir);
    }
    if (spec.venv.kind == PathKind::Linux) {
      out->arguments.insert(out->arguments.end(),
                            {L"--", L"bash", L"-lc",
                             L". \"$1\"; exec bash", L"dhepz",
                             spec.venv.activate_path});
    }
    return core::Ok();
  }

  if (!spec.wsl_distro.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"wsl_distro is valid only for WSL launches");
  }
  if (!IsWindowsPath(spec.working_dir)) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"PowerShell/Cmd working folder must be an absolute Windows path");
  }
  if (spec.venv.kind == PathKind::Linux) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Linux venv cannot be used by PowerShell or Cmd");
  }
  out->working_directory = spec.working_dir;
  if (spec.shell == Shell::PowerShell) {
    out->executable = L"powershell.exe";
    out->arguments = {L"-NoExit"};
    if (spec.venv.kind == PathKind::Windows) {
      if (!IsWindowsPath(spec.venv.activate_path) ||
          !EndsWithInsensitive(spec.venv.activate_path,
                               L"\\Scripts\\Activate.ps1")) {
        return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                         L"PowerShell venv must select Scripts\\Activate.ps1");
      }
      out->arguments.insert(out->arguments.end(),
                            {L"-ExecutionPolicy", L"Bypass", L"-File",
                             spec.venv.activate_path});
    }
  } else {
    out->executable = L"cmd.exe";
    if (spec.venv.kind == PathKind::Windows) {
      if (!IsWindowsPath(spec.venv.activate_path) ||
          !EndsWithInsensitive(spec.venv.activate_path,
                               L"\\Scripts\\activate.bat")) {
        return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                         L"Cmd venv must select Scripts\\activate.bat");
      }
      out->arguments = {L"/K", spec.venv.activate_path};
    }
  }
  return core::Ok();
}

}  // namespace terminal

#include "modules/terminal/terminal_logic.h"

#include <set>

namespace terminal {
namespace {

core::Status RequiredString(const json::Value& payload, std::wstring_view key,
                            std::wstring* out) {
  const json::Value* value = payload.Find(key);
  if (value == nullptr || !value->is_string() || value->AsString().empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"terminal payload requires non-empty '" +
                         std::wstring(key) + L"'");
  }
  *out = value->AsString();
  return core::Ok();
}

}  // namespace

core::Status ParseLaunchPayload(const json::Value& payload, LaunchSpec* out) {
  if (out == nullptr || !payload.is_object()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"terminal launch payload must be an object");
  }
  *out = {};
  static const std::set<std::wstring> allowed = {
      L"target", L"working_folder", L"wsl_distro", L"venv_enabled"};
  for (const auto& [key, value] : payload.members()) {
    (void)value;
    if (!allowed.contains(key)) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"unknown terminal payload field '" + key + L"'");
    }
  }

  std::wstring target;
  DHEPZ_RETURN_IF_ERROR(RequiredString(payload, L"target", &target));
  DHEPZ_RETURN_IF_ERROR(
      RequiredString(payload, L"working_folder", &out->working_folder));
  if (target == L"powershell") {
    out->target = Target::PowerShell;
  } else if (target == L"powershell_admin") {
    out->target = Target::PowerShellAdmin;
  } else if (target == L"wsl") {
    out->target = Target::Wsl;
    DHEPZ_RETURN_IF_ERROR(
        RequiredString(payload, L"wsl_distro", &out->wsl_distro));
  } else {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"terminal target is not supported");
  }

  if (const json::Value* enabled = payload.Find(L"venv_enabled");
      enabled != nullptr) {
    if (!enabled->is_bool()) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"venv_enabled must be a bool");
    }
    out->venv_enabled = enabled->AsBool();
  }
  return core::Ok();
}

core::Status BuildProcessRequest(const LaunchSpec& spec,
                                 modules::ProcessRequest* out) {
  if (out == nullptr || spec.working_folder.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"a working folder is required");
  }
  *out = {};
  out->operation = spec.target == Target::PowerShellAdmin
                       ? modules::ProcessOperation::ElevatedLaunch
                       : modules::ProcessOperation::Launch;
  out->executable = L"wt.exe";
  out->working_directory = spec.working_folder;
  out->arguments = {L"-w", L"new", L"new-tab"};

  if (spec.target == Target::Wsl) {
    if (spec.wsl_distro.empty()) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"WSL launch requires a distro");
    }
    out->arguments.insert(out->arguments.end(),
                          {L"wsl.exe", L"-d", spec.wsl_distro, L"--cd",
                           spec.working_folder});
    if (spec.venv_enabled) {
      out->arguments.insert(out->arguments.end(),
                            {L"--", L"bash", L"-lc",
                             L". ./.venv/bin/activate; exec bash -i"});
    }
    return core::Ok();
  }

  out->arguments.insert(out->arguments.end(),
                        {L"-p", L"PowerShell", L"-d", spec.working_folder});
  if (spec.venv_enabled) {
    out->arguments.insert(
        out->arguments.end(),
        {L"powershell.exe", L"-NoExit", L"-ExecutionPolicy", L"Bypass",
         L"-File", spec.working_folder + L"\\.venv\\Scripts\\Activate.ps1"});
  }
  return core::Ok();
}

}  // namespace terminal
